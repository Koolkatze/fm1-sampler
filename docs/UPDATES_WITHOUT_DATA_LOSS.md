# Actualizar FM1 Sampler sin perder datos (Android / GitHub)

## Lo que permite Android

**Una actualización instalada sobre el mismo paquete conserva `files/`, los proyectos `.fm1p`, escenas, SONG, grabaciones, WAVs, OFFICIAL, PACKS, ajustes y archivos de usuario** siempre que:

1. El `applicationId` sea **idéntico**: las previews usan `com.ferluht.fm1sampler.scenes`.
2. La APK nueva esté firmada con el **mismo certificado** que la instalada (o una rotación de clave admitida explícitamente por Android).
3. El `versionCode` sea mayor y se instale **encima de la anterior**, sin desinstalarla ni borrar datos (`pm clear`).
4. El código nuevo trate las migraciones como `merge-preserving`, sin sobrescribir archivos del usuario (así funciona `StorageLayout.initialize()`).

`adb install -r ruta.apk` y la instalación normal desde Descargas conservan los datos si se cumplen esas condiciones. **No se puede forzar a Android a ignorar una firma diferente** mediante `adb install -r`.

## Firmas de las versiones 0.38.11 y anteriores

Las APKs preview anteriores de GitHub Actions se firmaban con una **clave de depuración efímera**, generada por el runner de CI. Descargar la siguiente APK puede devolver `INSTALL_FAILED_UPDATE_INCOMPATIBLE`. Los certificados de esas versiones **no se pueden reconstruir desde una APK**. Por eso **no prometemos compatibilidad de actualización retroactiva**.

Para pasar desde una instalación con clave efímera a la **primera versión con firma persistente**, guarda y verifica tu biblioteca completa **fuera del móvil** antes de hacer ningún cambio:

- Dentro de LIBRARY, guarda primero los proyectos actuales y usa **SAVE BACKUP** para exportar el ZIP completo. Comprueba que el archivo existe y que es accesible fuera de la app.
- Alternativa ADB en equipos con `run-as`: `python tools/FM1_ADB_Recuperacion_0389.py revisar "FM1_BACKUP.tar"`; conserva la copia TAR y su checksum SHA-256. Los `.fm1p` son proyectos individuales, no sustitutos de un backup completo.
- **No desinstales la versión anterior** hasta haber comprobado el backup. Después de una migración de firma con copia verificada, instala la nueva APK y usa **IMPORT BACKUP** o el importador TAR.
- A partir de esa primera instalación con **el certificado estable**, las siguientes publicaciones con el mismo certificado podrán actualizarse sin desinstalar.

## Preparación del firmante permanente (solo propietario del repositorio)

No subas a GitHub el `.jks`, sus contraseñas ni los valores de GitHub Actions Secrets. Genera **una única identidad privada** en tu propio equipo y conserva copias de seguridad cifradas de ella. Ejemplo usando el JDK 17+:

```powershell
keytool -genkeypair -v -keystore fm1-updates.jks -alias fm1updates -keyalg RSA -keysize 3072 -validity 10000
```

Guarda todas las contraseñas en un gestor seguro, sin pasárselas por el chat a nadie. Comprueba el certificado que vas a fijar:

```powershell
keytool -list -v -keystore fm1-updates.jks -alias fm1updates
```

Copia el valor **SHA256** de la huella del certificado. En **GitHub → repositorio → Settings → Secrets and variables → Actions**, configura:

| Nombre | Tipo | Valor |
|---|---|---|
| `FM1_SIGNING_STORE_B64` | Repository Secret | Bytes del `.jks` codificados en Base64 |
| `FM1_SIGNING_STORE_PASSWORD` | Repository Secret | Contraseña del almacén |
| `FM1_SIGNING_KEY_ALIAS` | Repository Secret | Alias elegido, p. ej. `fm1updates` |
| `FM1_SIGNING_KEY_PASSWORD` | Repository Secret | Contraseña de la clave privada |
| `FM1_SIGNING_CERT_SHA256` | Repository **Variable** | Huella SHA-256 pública del certificado |

Para crear la cadena Base64 en PowerShell, sin escribirla en la consola ni en el repositorio:

```powershell
[Convert]::ToBase64String([IO.File]::ReadAllBytes((Resolve-Path ".\fm1-updates.jks"))) | Set-Clipboard
```

Pega el valor temporal del portapapeles directamente en el Secret de GitHub. Limpia el portapapeles después. **No publiques el Base64**: es la clave privada completa, no un identificador público.

El workflow `.github/workflows/publish-fm1-library-preview.yml` comprueba que las cuatro variables secretas existan antes de construir el APK, instala el almacén en el runner con permisos restrictivos, firma usando esa clave y verifica con `apksigner` que el certificado coincide con **`FM1_SIGNING_CERT_SHA256`** antes de publicar. Si falta alguna credencial o la huella es distinta, **falla de forma segura y no publica ninguna APK que rompería la cadena de actualizaciones**.

Los builds locales `assembleDebug` pueden usar la clave debug de desarrollo, pero **no son releases actualizables por defecto**; el CI de publicación comprueba explícitamente la clave estable.

## Cómo actualizar tras la primera versión firmada permanentemente

1. Guarda cambios no persistidos con **SAVE PROJECT**.
2. Es recomendable hacer un **SAVE BACKUP** periódico.
3. Comprueba que la nueva APK tenga el mismo `applicationId` y el certificado publicado.
4. Instálala encima con el gestor de archivos de Android o `adb install -r FM1-Sampler-Library-...apk`.
5. Abre la app y verifica proyectos, SONG, escenas, samples y packs.

La nueva biblioteca sigue bajo `files/FM1-SAMPLER/`. El código de migración utiliza `mergePreserving`; las 16 ranuras de proyecto mantienen los archivos `project_1.fm1p` ... `project_8.fm1p` originales y añaden `project_9.fm1p` ... `project_16.fm1p` sin renumerar.

**No existe una actualización transparente y segura desde un certificado antiguo cuya clave privada ya no está disponible.** La migración de datos mediante backup verificado es el único camino legítimo y conservador en ese caso.
