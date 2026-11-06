#!/usr/bin/env python3
"""FM1 Sampler 0.38.9: recuperacion ADB segura de TAR y proyectos FMP2.

  python FM1_ADB_Recuperacion_0389.py revisar FM1_BACKUP.tar
  python FM1_ADB_Recuperacion_0389.py restaurar FM1_BACKUP.tar --app scenes
  python FM1_ADB_Recuperacion_0389.py importar-fm1p proyecto_2.fm1p --slot 1 --aplicar
  python FM1_ADB_Recuperacion_0389.py importar-fm1p proyecto_2.fm1p --slot 1 --aplicar --reemplazar

No usa ZIP ni 'tar xf -' en Android. Staging via 'adb push' y tuberia interna
AL TELEFONO para que Windows no altere bytes binarios; verifica SHA-256
antes de publicar cada archivo en la biblioteca. Nunca desinstala ni borra datos.
"""
from __future__ import annotations

import argparse
from datetime import datetime
import hashlib
import os
from pathlib import Path, PurePosixPath
import re
import shlex
import struct
import subprocess
import sys
import tarfile
import tempfile
import uuid

PACKAGES = {"scenes": "com.ferluht.fm1sampler.scenes",
            "original": "com.ferluht.fm1sampler"}
LIBRARY = "files/FM1-SAMPLER"
PROJECTS = LIBRARY + "/PROYECTOS"
STAGE_ROOT = "/data/local/tmp"
CHUNK = 1024 * 1024
MAX_PROJECT = 536_870_912 + 1_000_000


class FM1Error(RuntimeError):
    pass


def fail(message):
    raise FM1Error(message)


def adb(*args, input_file=None, stdout=None, check=True):
    """Never pass byte streams through a Windows console or PowerShell redirection."""
    if input_file is not None:
        fail("Prohibida la entrada binaria por adb shell: utilizar adb push.")
    try:
        result = subprocess.run([os.getenv("FM1_ADB_BIN", "adb"), *args],
                                stdout=subprocess.PIPE if stdout is None else stdout,
                                stderr=subprocess.PIPE, check=False)
    except FileNotFoundError:
        fail("No se encuentra adb. Instala Platform Tools y añade adb.exe al PATH.")
    if check and result.returncode:
        out = (result.stderr or b"") + (result.stdout or b"")
        fail("ADB devolvio un error: " + out.decode("utf-8", "replace")[:800].strip())
    return result


def sh(pkg, command, check=True):
    return adb("shell", "-T", "run-as " + shlex.quote(pkg) +
               " sh -c " + shlex.quote(command), check=check)


def remote_exists(pkg, path):
    r = sh(pkg, "test -e " + shlex.quote(path), check=False)
    if r.returncode not in (0, 1):
        fail("No se pudo comprobar archivo privado: " + r.stderr.decode("utf-8", "replace"))
    return r.returncode == 0


def check_device(pkg):
    r = adb("get-state")
    if r.stdout.strip() != b"device":
        fail("No hay telefono ADB autorizado; verifica 'adb devices'.")
    r = adb("shell", "run-as", pkg, "pwd")
    if b"/data/" not in r.stdout:
        fail("La APK no permite 'run-as' (se requiere APK debuggable).")
    print("ADB autorizado; paquete " + pkg)


def stop_app(pkg):
    adb("shell", "am", "force-stop", pkg)


def sha_file(path):
    h = hashlib.sha256()
    count = 0
    with open(path, "rb") as f:
        for buf in iter(lambda: f.read(CHUNK), b""):
            h.update(buf)
            count += len(buf)
    return h.hexdigest(), count


def pull_digest(pkg, path, save_as=None):
    """adb exec-out preserves exact remote bytes on Windows and Unix."""
    h = hashlib.sha256()
    total = 0
    filename = [os.getenv("FM1_ADB_BIN", "adb"), "exec-out", "run-as", pkg, "cat", path]
    try:
        with subprocess.Popen(filename, stdout=subprocess.PIPE, stderr=subprocess.PIPE) as p:
            assert p.stdout is not None
            if save_as is None:
                while True:
                    block = p.stdout.read(CHUNK)
                    if not block:
                        break
                    h.update(block)
                    total += len(block)
            else:
                with open(save_as, "xb") as out:
                    while True:
                        block = p.stdout.read(CHUNK)
                        if not block:
                            break
                        out.write(block)
                        h.update(block)
                        total += len(block)
                if total == 0:
                    fail("La copia del archivo existente esta vacia; importacion cancelada.")
            err = p.stderr.read().decode("utf-8", "replace")
            rc = p.wait()
        if rc:
            fail("No se pudo leer archivo privado por ADB: " + err[:500])
    except (OSError, FM1Error):
        if save_as is not None:
            Path(save_as).unlink(missing_ok=True)
        raise
    return h.hexdigest(), total


def copy_to_private(pkg, local, dest, replacing=False):
    """Staged upload + phone-side pipe + verified atomic rename; no host->shell stdin."""
    tag = uuid.uuid4().hex
    stage = STAGE_ROOT + "/fm1-stage-" + tag
    temp = dest.rsplit("/", 1)[0] + "/.fm1-import-" + tag + ".part"
    expected_hash, expected_size = sha_file(local)
    parent = dest.rsplit("/", 1)[0]
    if remote_exists(pkg, dest) and not replacing:
        return "skip"
    sh(pkg, "mkdir -p " + shlex.quote(parent))
    success = False
    try:
        # adb push uses adb's binary sync protocol, not PowerShell redirection.
        adb("push", str(local), stage)
        # Run the pipe ON THE PHONE: the Android shell user reads /data/local/tmp;
        # run-as (app UID) writes the data to app-private temporary storage.
        inner = "cat > " + shlex.quote(temp)
        pipe = ("cat " + shlex.quote(stage) + " | run-as " +
                shlex.quote(pkg) + " sh -c " + shlex.quote(inner))
        adb("shell", "-T", pipe)
        got_hash, got_size = pull_digest(pkg, temp)
        if (got_hash, got_size) != (expected_hash, expected_size):
            fail("SHA-256 FALLIDO en temporal: origen " + expected_hash +
                 " / destino " + got_hash + f"; bytes {expected_size} / {got_size}. "
                 "El archivo original no se ha sustituido.")
        if replacing:
            sh(pkg, "mv -f " + shlex.quote(temp) + " " + shlex.quote(dest))
        else:
            cmd = ("if test -e " + shlex.quote(dest) +
                   "; then exit 47; fi; mv " + shlex.quote(temp) + " " + shlex.quote(dest))
            result = sh(pkg, cmd, check=False)
            if result.returncode == 47:
                success = True   # Nothing overwritten; temporary part is discarded below.
                return "skip"
            if result.returncode:
                fail("No se pudo instalar el archivo: " + result.stderr.decode("utf-8", "replace"))
        got_hash, got_size = pull_digest(pkg, dest)
        if (got_hash, got_size) != (expected_hash, expected_size):
            fail("Fallo CRITICO verificando el archivo final. Conserva todos los respaldos; "
                 f"esperado {expected_hash}, recibido {got_hash}.")
        success = True
        return "ok"
    finally:
        # Only remove temporary data; NEVER remove an existing project here.
        sh(pkg, "rm -f " + shlex.quote(temp), check=False)
        adb("shell", "-T", "rm -f " + shlex.quote(stage), check=False)
        if not success:
            print("NOTA: se conservan intactos los archivos locales de respaldo.")


def check_fm1p(path):
    if not path.is_file() or path.suffix.lower() != ".fm1p":
        fail("El .fm1p indicado no existe: " + str(path))
    with path.open("rb") as f:
        head = f.read(16)
    if len(head) != 16:
        fail("Proyecto .fm1p demasiado pequeño.")
    magic, version, rate, size = struct.unpack("<4I", head)
    if magic != 0x32504D46 or version != 2 or not (8000 <= rate <= 192000) or not (1 <= size <= 65536):
        fail("Cabecera FMP2 incompatible; no importar.")
    checksum, bytes_ = sha_file(path)
    if bytes_ <= 24 or bytes_ > MAX_PROJECT:
        fail("El tamaño del proyecto es invalido.")
    print(f"Proyecto FMP2 cabecera correcta: {path.name}, {bytes_:,} bytes, {rate} Hz, SHA-256 {checksum}")
    print("El motor de FM1 Sampler validara el contenido FMP2 completo al cargarlo.")


def import_project(path, slot, pkg, apply, replace):
    check_fm1p(path)
    target = f"{PROJECTS}/project_{slot}.fm1p"
    print("Destino: " + target)
    if not apply:
        print("Simulacion terminada. Usa --aplicar para transferir.")
        return
    check_device(pkg)
    stop_app(pkg)
    old = remote_exists(pkg, target)
    if old and not replace:
        fail(f"El slot {slot} ya existe: se ha evitado sobrescribir. "
             "Elige otro --slot o usa --reemplazar tras guardar una copia.")
    if old:
        recovery = Path.cwd() / ("FM1_ORIGINALES_ADB_" + datetime.now().strftime("%Y%m%d_%H%M%S") +
                                 "_" + uuid.uuid4().hex[:6])
        recovery.mkdir(exist_ok=False)
        for name in [f"project_{slot}.fm1p", f"project_{slot}.name"]:
            src = PROJECTS + "/" + name
            if remote_exists(pkg, src):
                dest = recovery / name
                read_hash, read_size = pull_digest(pkg, src, save_as=dest)
                verify_hash, verify_size = pull_digest(pkg, src)
                if (read_hash, read_size) != (verify_hash, verify_size):
                    fail("No se pudo verificar el respaldo del slot existente. Cancelado.")
                print("  Original guardado en " + str(dest))
    print("Enviando proyecto mediante ADB push y canal interno Android...")
    answer = copy_to_private(pkg, path, target, replacing=replace)
    if answer != "ok": fail("El slot se ocupo durante la transferencia; no sobrescrito.")
    print(f"PROYECTO INSTALADO Y VERIFICADO: slot {slot}. Abre PROJECT > LOAD PROJECT.")


def safe_name(raw):
    if not isinstance(raw, str) or "\\" in raw or "\x00" in raw or raw.startswith("/"):
        fail("Ruta TAR no segura: " + repr(raw))
    parts = PurePosixPath(raw.removeprefix("./")).parts
    if not parts or parts[0] != "files" or any(x in ("..", "") for x in parts):
        fail("Ruta TAR fuera de files/: " + repr(raw))
    if any("\n" in x or "\r" in x for x in parts):
        fail("Nombre TAR con salto de linea no admitido.")
    return "/".join(parts)


def map_old_to_current(src):
    """Old 0.37 files/ tree -> 0.38.9 FM1-SAMPLER tree; no destructive moves."""
    if src.startswith(LIBRARY + "/"):
        return src, 2
    prefix = "files/"
    rest = src[len(prefix):]
    if re.fullmatch(r"project_[1-8]\.fm1p(?:\.bak)?", rest) or re.fullmatch(r"project_[1-8]\.name", rest) or rest.startswith("fm1_scenes") or rest.startswith("project_assets/") or rest.startswith("projects/"):
        return PROJECTS + "/" + rest, 1
    if re.fullmatch(r"sample_.*\.wav", rest, re.IGNORECASE):
        return LIBRARY + "/SAVED/RECORDINGS/" + rest, 1
    if rest.startswith("samples/"):
        sub = rest[len("samples/"):]
        if not sub or sub == ".v2" or sub.startswith(".v2/"):
            return None, 0
        if sub.startswith("SAVED/"):
            chunks = sub.split("/")
            if len(chunks) >= 2:
                chunks[1] = {"loops": "LOOPS", "recordings": "RECORDINGS", "imports": "IMPORTS"}.get(chunks[1].lower(), chunks[1])
            return LIBRARY + "/" + "/".join(chunks), 1
        if sub.startswith("OFFICIAL/"):
            return LIBRARY + "/" + sub, 1
        category = sub.split("/", 1)[0]
        if category in ("Bass", "Drums", "Keys", "Pads"):
            return LIBRARY + "/OFFICIAL/" + sub, 1
        return LIBRARY + "/PACKS/" + sub, 1
    return src, 0


def inspect_tar(path):
    if not path.is_file(): fail("No existe el TAR " + str(path))
    files = {}
    total = 0
    with tarfile.open(path, "r:") as stream:
        for member in stream:
            source = safe_name(member.name)
            if member.isdir():
                continue
            if not member.isfile():
                fail("TAR contiene enlace o tipo de archivo no permitido: " + source)
            mapped, priority = map_old_to_current(source)
            if mapped is None:
                continue
            if member.size < 0 or member.size > 8_000_000_000:
                fail("Tamaño no admitido en TAR: " + source)
            if mapped in files:
                previous = files[mapped]
                if priority == previous[2]:
                    fail("TAR ambiguo: dos archivos para la misma ruta " + mapped)
                if priority < previous[2]:
                    continue
            # Read each entry completely to catch truncated backup bytes.
            f = stream.extractfile(member)
            if f is None: fail("No se puede leer TAR: " + source)
            read = 0
            while True:
                block = f.read(CHUNK)
                if not block: break
                read += len(block)
            if read != member.size:
                fail("TAR truncado: " + source)
            files[mapped] = (source, member.size, priority)
    if not files: fail("TAR sin archivos compatibles.")
    total = sum(v[1] for v in files.values())
    projects = sorted(x for x in files if x.lower().endswith(".fm1p"))
    wavs = [x for x in files if x.lower().endswith(".wav")]
    digest, file_size = sha_file(path)
    print(f"TAR VALIDO: {path.name}, {file_size / 1048576:.1f} MiB; "
          f"{len(files)} archivos; {total / 1048576:.1f} MiB de datos")
    print(f"Proyectos .fm1p: {len(projects)}; WAV: {len(wavs)}; SHA-256 TAR: {digest}")
    return files


def list_remote_files(pkg):
    r = adb("exec-out", "run-as", pkg, "find", "files", "-type", "f")
    names = set()
    for line in r.stdout.decode("utf-8", "replace").splitlines():
        name = line.strip().removeprefix("./")
        if name.startswith("files/"):
            names.add(name)
    return names


def restore_tar(path, pkg, apply=True):
    entries = inspect_tar(path)
    check_device(pkg)
    if apply:
        stop_app(pkg)
    current = list_remote_files(pkg)
    pending = [(dest, src, size) for dest, (src, size, _) in entries.items() if dest not in current]
    skipped = [(dest, src) for dest, (src, _, _) in entries.items() if dest in current]
    conflicts = [dest for dest, src in skipped if dest.lower().endswith(".fm1p")]
    print(f"Se importaran {len(pending)} archivos nuevos; "
          f"se conservaran {len(skipped)} existentes, sin sobrescribir.")
    if skipped:
        print("Ejemplos de archivos ya existentes (CONSERVADOS): " +
              ", ".join(dest for dest, _ in skipped[:4]))
    if conflicts:
        print("AVISO: Proyectos existentes conservados: " + ", ".join(conflicts[:10]))
        print("Estos proyectos NO se han comparado ni sustituido por los del TAR.")
        print("Si deseas reemplazar uno, usa importar-fm1p --reemplazar (copia el original antes).")
    if not apply:
        print("SIMULACION: no se ha cambiado ningun archivo Android.")
        return
    if not pending:
        print("No hay archivos nuevos que restaurar; la copia TAR no se ha descartado.")
        return
    # The TAR is a container *on the PC*. Extract each payload into a PC-side
    # temporary file, then use adb push + phone-side pipe, not Android 'tar xf -'.
    completed = 0
    failed = []
    with tarfile.open(path, "r:") as stream, tempfile.TemporaryDirectory(prefix="fm1-adb-restore-") as td:
        members = {safe_name(m.name): m for m in stream.getmembers() if m.isfile()}
        for index, (dest, source, size) in enumerate(sorted(pending), 1):
            print(f"[{index}/{len(pending)}] {source} -> {dest} ({size:,} bytes)", flush=True)
            try:
                member = members[source]
                reader = stream.extractfile(member)
                if reader is None: fail("Entrada TAR ilegible: " + source)
                local = Path(td) / "fm1_payload.bin"
                with local.open("wb") as out:
                    count = 0
                    while True:
                        block = reader.read(CHUNK)
                        if not block: break
                        out.write(block)
                        count += len(block)
                if count != size: fail("Archivo TAR incompleto: " + source)
                result = copy_to_private(pkg, local, dest, replacing=False)
                if result == "ok": completed += 1
                else: print("  OMITIDO: aparecio otro archivo con el mismo nombre.")
                local.unlink(missing_ok=True)
            except (FM1Error, OSError) as exc:
                print("  ERROR en " + dest + ": " + str(exc))
                failed.append(dest)
                break  # Do not continue to mutate anything after unexpected failure
    if failed:
        fail(f"Restauracion interrumpida. Importados {completed}; "
             f"errores {len(failed)}. Conserva el TAR y revisa el registro. "
             "No se han sobrescrito archivos existentes.")
    print(f"RESTAURACION TERMINADA: {completed} archivos nuevos con SHA-256 verificado; "
          f"{len(skipped)} existentes conservados.")
    print("Reabre FM1 Sampler y usa PROJECT > LOAD PROJECT para comprobar tus proyectos.")
    print("NO borres el TAR: conserva una copia externa verificada.")


def backup_tar(pkg, output):
    check_device(pkg)
    stop_app(pkg)
    output = output or Path.cwd() / ("FM1_BACKUP_" + datetime.now().strftime("%Y%m%d_%H%M%S") + "_scenes.tar")
    if output.exists(): fail("No sobrescribimos el TAR " + str(output))
    partial = Path(str(output) + ".incompleto")
    try:
        with partial.open("wb") as target:
            adb("exec-out", "run-as", pkg, "tar", "cf", "-", "files", stdout=target)
        inspect_tar(partial)
        partial.replace(output)
        print("Copia completa guardada en " + str(output))
    finally:
        partial.unlink(missing_ok=True)


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    # Backwards compatibility with the old standalone FM1P script:
    # python FM1_ADB_Importar_FM1P.py project_2.fm1p --slot 1 --aplicar
    if argv and argv[0].lower().endswith(".fm1p"):
        argv.insert(0, "importar-fm1p")
    p = argparse.ArgumentParser(description="FM1 Sampler 0.38.9: TAR completo o proyecto FM1P mediante ADB")
    sub = p.add_subparsers(dest="action", required=True)
    for action in ("comprobar", "copia", "restaurar", "revisar", "importar-fm1p"):
        x = sub.add_parser(action)
        if action in ("restaurar", "revisar"):
            x.add_argument("archivo", type=Path)
        if action == "importar-fm1p":
            x.add_argument("archivo", type=Path)
            x.add_argument("--slot", type=int, required=True, choices=range(1, 9))
            x.add_argument("--aplicar", action="store_true")
            x.add_argument("--reemplazar", action="store_true")
        if action == "copia":
            x.add_argument("--salida", type=Path)
        if action == "restaurar":
            x.add_argument("--simular", action="store_true")
        if action != "revisar":
            x.add_argument("--app", choices=PACKAGES, default="scenes")
    args = p.parse_args(argv)
    if args.action == "revisar":
        inspect_tar(args.archivo)
        return
    pkg = PACKAGES[args.app]
    if args.action == "comprobar":
        check_device(pkg)
    elif args.action == "copia":
        backup_tar(pkg, args.salida)
    elif args.action == "restaurar":
        restore_tar(args.archivo, pkg, not args.simular)
    elif args.action == "importar-fm1p":
        if args.reemplazar and not args.aplicar:
            fail("--reemplazar requiere --aplicar")
        import_project(args.archivo, args.slot, pkg, args.aplicar, args.reemplazar)


if __name__ == "__main__":
    try:
        main()
    except (FM1Error, OSError, tarfile.TarError, ValueError) as error:
        print("ERROR: " + str(error), file=sys.stderr)
        sys.exit(1)