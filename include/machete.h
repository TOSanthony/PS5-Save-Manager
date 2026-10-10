#ifndef MACHETE_H
#define MACHETE_H

#include <stdint.h>
#include <stdbool.h>

// Definiciones de la aplicación
#define APP_NAME "MacheteSVG"
#define APP_VERSION "0.1.0"

#ifdef __cplusplus
extern "C" {
#endif

// Prototipos iniciales para los subsistemas
void init_graphics();
void init_save_mounting();
void init_cloud_services();

#ifdef __cplusplus
}
#endif

#endif // MACHETE_H
