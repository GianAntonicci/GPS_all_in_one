#pragma once
// Strings do GPX 1.1. O rodapé tem tamanho fixo: cada ponto é escrito por cima dele e o rodapé
// é reescrito, então o arquivo é XML válido depois de cada ponto.
#include <cstddef>
#include <cstdint>
#include "gps_logic.h"

extern const char kGpxFooter[];  // "</trkseg></trk></gpx>\n"

size_t gpxHeader(char* out, size_t cap);  // comprimento escrito ou 0 se não couber
size_t gpxPoint(char* out, size_t cap, double lat, double lon, float ele, const DateTime& utc,
                bool breakSegment);
// "/tracks/AAAAMMDD_HHMMSS.gpx"; suffix >= 2 acrescenta "_N".
void gpxFileName(char* out, size_t cap, const DateTime& local, uint8_t suffix);
