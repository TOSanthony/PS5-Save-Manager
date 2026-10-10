# Contributing to MacheteSVG / Guía de Contribución

Thank you for your interest in contributing to MacheteSVG. Please read the guidelines below before submitting pull requests or opening issues.

Gracias por tu interés en contribuir a MacheteSVG. Por favor lee las siguientes pautas antes de enviar pull requests o abrir reportes de error.

---

## English Guidelines

### Development Principles
1. **Zero Data Loss Guarantee:** Any code touching the PlayStation 5 filesystem, Keystone validation, or save container unmounting (`sceSaveDataUmount`) must adhere to strict defensive programming and never bypass cryptographic safety checks.
2. **Deterministic Builds:** The codebase targets the PlayStation 5 homebrew ecosystem (`pacbrew-packages`, `ps5-payload-sdk`). Changes must maintain reproducible compilation without proprietary Sony SDK headers.
3. **No Hardcoded Strings:** All user-facing strings must use the centralized `machete::i18n::tr()` localization table. Every new string must include both English and Spanish translations.
4. **Professional Documentation:** All documentation, comments, release notes, and commits must remain strictly professional, bilingual (English first, followed by Spanish), and completely free of emojis.

### Submission Workflow
1. Fork the repository and create a feature branch (`feature/your-feature-name` or `bugfix/issue-description`).
2. Run test suites locally before committing:
   ```bash
   make test-unit
   ```
3. Ensure code formatting matches the project style:
   ```bash
   clang-format -i src/**/*.cpp src/**/*.hpp
   ```
4. Commit your changes with descriptive, concise commit messages.
5. Push to your fork and submit a Pull Request against the `main` branch.

---

## Pautas en Español (Spanish Guidelines)

### Principios de Desarrollo
1. **Garantía de Cero Pérdida de Datos:** Cualquier código que interactúe con el sistema de archivos de PlayStation 5, validación de Keystone o desmontaje de contenedores de guardado (`sceSaveDataUmount`) debe seguir una programación defensiva estricta y nunca evadir las comprobaciones criptográficas de seguridad.
2. **Compilación Determinista:** La base de código apunta al ecosistema de homebrew de PlayStation 5 (`pacbrew-packages`, `ps5-payload-sdk`). Los cambios deben mantener una compilación reproducible sin cabeceras propietarias del SDK oficial de Sony.
3. **Sin Textos Hardcodeados:** Todas las cadenas de texto visibles deben utilizar la tabla centralizada de localización `machete::i18n::tr()`. Cada nueva cadena debe incluir sus correspondientes traducciones al inglés y al español.
4. **Documentación Profesional:** Toda la documentación, comentarios, notas de lanzamiento y mensajes de confirmación de git deben mantenerse estrictamente profesionales, bilingües (inglés primero, seguido de español) y completamente libres de emojis.

### Flujo de Trabajo para Contribuciones
1. Haz un fork del repositorio y crea una rama para tu funcionalidad (`feature/nombre-de-funcionalidad` o `bugfix/descripcion-del-fallo`).
2. Ejecuta las pruebas unitarias localmente antes de confirmar los cambios:
   ```bash
   make test-unit
   ```
3. Asegura el formato de código según el estilo del proyecto:
   ```bash
   clang-format -i src/**/*.cpp src/**/*.hpp
   ```
4. Confirma tus cambios con mensajes de commit claros, profesionales y descriptivos.
5. Envía tu Pull Request hacia la rama `main` del repositorio oficial.
