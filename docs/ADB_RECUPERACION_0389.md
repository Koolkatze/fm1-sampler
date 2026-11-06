# FM1 Sampler 0.38.9 — ADB corregido para TAR y `.fm1p` (Windows)

Esta versión **sustituye** a `Migracion_Segura_FM1.py` y `FM1_ADB_Importar_FM1P.py` entregados antes. Aquellos usaban `tar xf -` por ADB o enviaban bytes binarios al `stdin` de `adb shell`; en tu teléfono dieron los errores `EOF: Illegal seek` y SHA-256 diferente. **No repitas esos scripts antiguos.**

El archivo `FM1_ADB_Recuperacion_0389.py` hace las dos operaciones. Se incluyen también dos copias con nombres `..._CORREGIDO.py` por comodidad; son **el mismo código** y se pueden usar con los mismos parámetros.

## 1. Requisitos y precauciones

- Windows 10/11 con Python 3.10+; Android SDK **Platform Tools** (`adb.exe`) en `PATH`.
- Depuración USB habilitada y PC autorizado. Comprueba con `adb devices`.
- APK debug del FM1 Sampler 0.38.9 con paquete **`com.ferluht.fm1sampler.scenes`**; `run-as` debe estar permitido. No funciona en APKs que prohíban `run-as`.
- **No desinstales FM1 Sampler, no uses Borrar datos, no borres ni sustituyas el TAR original.** Si tienes proyectos en memoria, haz SAVE PROJECT y termina grabaciones antes de importar.
- No modifica firmware del FM-1 físico. No necesitas instalar otra APK.

## 2. Restaurar el TAR de backup (tus cinco proyectos y biblioteca)

Descomprime `FM1_ADB_0389_CORREGIDO.zip` en Descargas, abre **PowerShell** y ejecuta:

```powershell
cd "$env:USERPROFILE\Downloads\FM1_ADB_0389_CORREGIDO"
python .\FM1_ADB_Recuperacion_0389.py revisar "C:\Users\serio\Downloads\FM1_BACKUP_20261105_210357_scenes.tar"
python .\FM1_ADB_Recuperacion_0389.py restaurar "C:\Users\serio\Downloads\FM1_BACKUP_20261105_210357_scenes.tar" --app scenes --simular
```

**Solo cuando las dos comprobaciones terminen correctamente**, importa los archivos que faltan:

```powershell
python .\FM1_ADB_Recuperacion_0389.py restaurar "C:\Users\serio\Downloads\FM1_BACKUP_20261105_210357_scenes.tar" --app scenes
```

### Qué ha cambiado para evitar el error

- El TAR **se lee en el ordenador**. No se usa `tar xf -` en el móvil (que devolvía `EOF: Illegal seek`).
- Cada archivo faltante se envía con **`adb push`** a un archivo temporal en `/data/local/tmp/`; Android lo copia mediante una **tubería interna al propio teléfono**, usando `run-as` para acceder a la biblioteca privada.
- El temporal dentro de la aplicación se compara con el original mediante **SHA-256** (usando `adb exec-out`), antes de colocarlo en su ubicación final. Luego se verifica de nuevo.
- Convierte las rutas antiguas a la biblioteca de la **versión 0.38.9**: `files/project_2.fm1p` → `files/FM1-SAMPLER/PROYECTOS/project_2.fm1p`, los WAV `sample_*.wav` → `SAVED/RECORDINGS`, carpetas `files/samples/...` → `OFFICIAL`, `PACKS` o `SAVED`.
- **Los archivos que ya existen se conservan, incluidos los sonidos oficiales** como `Bass/Acoustic.wav`. Por eso **no se cancela** todo el TAR por encontrar sonidos preinstalados.
- **Tampoco se sobrescriben proyectos existentes**. El programa los enumera; esos archivos **no se comparan** con los del TAR. Si necesitas recuperar un proyecto concreto por encima de otro, usa el apartado siguiente, con copia previa.
- Ante error, se interrumpe el proceso y se conservan todos los archivos del TAR. Puede haber archivos nuevos ya importados correctamente; la siguiente ejecución omitirá los que existan. **No se eliminan datos de usuario.**

## 3. Importar un solo `.fm1p`

Un `.fm1p` de proyecto individual puede estar dentro de tu carpeta extraída `...\files\` o de `...\files\FM1-SAMPLER\PROYECTOS\`. Si lo has extraído en la primera, ejemplo:

```powershell
python .\FM1_ADB_Recuperacion_0389.py importar-fm1p "C:\Users\serio\Downloads\FM1_BACKUP_20261105_210357_scenes\files\project_2.fm1p" --slot 1
python .\FM1_ADB_Recuperacion_0389.py importar-fm1p "C:\Users\serio\Downloads\FM1_BACKUP_20261105_210357_scenes\files\project_2.fm1p" --slot 1 --aplicar
```

El primer comando valida sin transferir. El segundo importa al **slot 1 solo si está vacío**. Si ya contiene un proyecto, se detiene y no sobrescribe nada. Puedes elegir otro slot del 1 al 8.

**Reemplazo deliberado de un slot:** solo tras guardar también un backup de los proyectos actuales, añade `--reemplazar`. Se hará primero una copia del `.fm1p` y del `.name` original, si existe, dentro de una carpeta `FM1_ORIGINALES_ADB_...` en el ordenador. **Verifica y conserva esa carpeta.**

```powershell
python .\FM1_ADB_Recuperacion_0389.py importar-fm1p "C:\Users\serio\Downloads\FM1_BACKUP_20261105_210357_scenes\files\project_2.fm1p" --slot 1 --aplicar --reemplazar
```

No ejecutes `--reemplazar` si todavía no has decidido qué proyecto del slot conservar. La etiqueta del proyecto puede mantener el nombre antiguo: se puede renombrar al guardarlo en la app.

## 4. Si aparecen más errores

- `No se encuentra adb`: instala Platform Tools y añade la carpeta de `adb.exe` al `PATH`.
- `run-as: package not debuggable` o `Unknown package`: verifica la variante instalada (no desinstales nada).
- `SHA-256 FALLIDO en temporal`: **no se publica ni sobrescribe ese archivo**; copia aquí el error completo, indicando los bytes esperados y recibidos.
- `AVISO: Proyectos existentes conservados`: el programa protegió slots ocupados; puede requerirse importar uno por uno a slots vacíos o hacer un reemplazo **explícito** después de sacar respaldo.
- Si ves `Permission denied` al usar la tubería interna, copia el mensaje completo; Android puede restringir algunas operaciones dependiendo del dispositivo/SELinux.

Después de restaurar, abre la app y verifica **PROJECT → LOAD PROJECT**, tus clips y loops. El header FMP2 se comprueba en Python, pero la app verifica la estructura interna completa al cargar el proyecto.

## 5. Pruebas y limitaciones

Se han ejecutado **8 pruebas de host** con un dispositivo ADB simulado: transferencia de bytes exacta, checksum fallido, protección de archivos existentes, restauración TAR con sonido oficial en conflicto, rutas heredadas, TAR malicioso, importación `.fm1p` y copia de seguridad antes de reemplazar un slot. **No se ha probado todavía en el Nothing Phone 3 físico.**