# Preservar proyectos, loops y grabaciones de FM1 Sampler antes de actualizar

> **NO desinstales la versión anterior, NO uses «Borrar datos» y NO fuerces una actualización si Android indica un conflicto de firma.**

## Qué se guarda

La versión Scenes 0.37 guarda los proyectos completos en el almacenamiento privado:
`files/project_1.fm1p` ... `project_8.fm1p`, junto a las escenas
`files/fm1_scenes.dat` y sus fuentes de audio. Algunos loops guardados con el botón
SAVE de versiones antiguas son WAV con nombres `files/sample_*.wav`.
Los sonidos copiados o importados están en `files/samples/`.
Una copia completa de `files/` preserva todos esos elementos.

La versión 0.38 migra **los archivos de la misma instalación** hacia
`files/FM1-SAMPLER/{PROYECTOS,OFFICIAL,SAVED,PACKS}` en su primer arranque.
No sobrescribe archivos existentes. Una actualización directa solo funciona si
**el identificador de paquete, la firma y el código de versión lo permiten**;
las Releases de depuración de distintas ejecuciones de CI pueden tener firmas
distintas. Ante un conflicto de firma no desinstales a ciegas.

## Copia sin root mediante ADB (APKs de depuración)

Requiere un ordenador con Android SDK Platform Tools, depuración USB activa en
el teléfono y autorización del ordenador. El APK antiguo debe ser *debuggable*,
como las compilaciones `assembleDebug`; en una APK de producción Android puede
rechazar `run-as`.

1. **Guarda primero todos tus proyectos dentro de la aplicación antigua y ciérrala.**
2. Comprueba la conexión y que el paquete tiene acceso `run-as`:

   ```sh
   adb devices
   adb shell run-as com.ferluht.fm1sampler.scenes pwd
   ```

3. **Copia todos los archivos (sin modificar el original)**:

   ```sh
   adb exec-out run-as com.ferluht.fm1sampler.scenes tar cf - files > FM1_BACKUP.tar
   ```

   En PowerShell, **no uses redirección de texto** para un TAR binario;
   usa la herramienta Python `FM1_Migracion_Segura.py` del chat, que escribe
   los bytes sin conversión de línea.

4. **Comprueba** el contenido, el tamaño y la integridad de `FM1_BACKUP.tar`
   antes de cambiar APKs. Conserva una copia adicional en otro disco.

5. Si Android permite instalar 0.38 encima de 0.37 con firma compatible,
   los datos se conservarán en la instalación y el primer arranque los migrará:
   **no hace falta restaurar el TAR**.

6. Si Android rechaza la firma, la solución segura es copiar y verificar los
   datos primero, instalar la nueva aplicación y restaurar `files/` **antes
   de abrirla**. La utilidad Python comprueba colisiones y se niega a sobrescribir.
   **No automatiza ninguna desinstalación**.

   En una terminal Unix con un destino nuevo sin archivos en conflicto:

   ```sh
   adb shell run-as com.ferluht.fm1sampler.scenes mkdir -p files
   adb shell -T run-as com.ferluht.fm1sampler.scenes tar xf - < FM1_BACKUP.tar
   ```

   La misma operación puede hacerse con `FM1_Migracion_Segura.py restaurar
   FM1_BACKUP.tar --app scenes` para comprobar los conflictos antes de escribir.

7. Abre FM1 Sampler 0.38. Revisa la biblioteca **PROYECTOS** y
   **SAVED/RECORDINGS** (los WAV antiguos `sample_*.wav` pueden proceder de
   antiguas operaciones SAVE LOOP o de micrófono, indistinguibles por nombre).
   Verifica tus proyectos y loops **antes de eliminar cualquier respaldo**.

## Paquetes Android

- `com.ferluht.fm1sampler.scenes`: las Releases Scenes 0.37 y Library 0.38.
- `com.ferluht.fm1sampler`: otra variante original; requiere
  `--app original` al hacer la copia, y `--app scenes` al restaurarla
  en la nueva versión. Pueden coexistir y no se pueden leer mutuamente
  los archivos privados directamente.

**Limitaciones:** los clips que solo existían en memoria y jamás se guardaron
en archivos no pueden rescatarse con este método. Los APK no depurables
bloquean `run-as`, y no debe intentarse acceder a los datos de otras
aplicaciones sin autorización del usuario. La migración no se ha validado
en un Nothing Phone 3 real. A partir de ahora conviene configurar una clave
estable de firma en los secretos de GitHub Actions para que futuras versiones
puedan actualizarse sin este procedimiento.
