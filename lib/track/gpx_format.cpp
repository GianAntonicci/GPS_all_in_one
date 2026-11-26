#include "gpx_format.h"

#include <cstdio>

const char kGpxFooter[] = "</trkseg></trk></gpx>\n";

namespace {
size_t fit(int n, size_t cap) { return (n > 0 && (size_t)n < cap) ? (size_t)n : 0; }
}  // namespace

size_t gpxHeader(char* out, size_t cap) {
  int n = snprintf(out, cap,
                   "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                   "<gpx version=\"1.1\" creator=\"GPS_all_in_one\" "
                   "xmlns=\"http://www.topografix.com/GPX/1/1\">\n"
                   "<trk><trkseg>\n");
  return fit(n, cap);
}

size_t gpxPoint(char* out, size_t cap, double lat, double lon, float ele, const DateTime& utc,
                bool breakSegment) {
  int n = snprintf(out, cap,
                   "%s<trkpt lat=\"%.6f\" lon=\"%.6f\"><ele>%.1f</ele>"
                   "<time>%04u-%02u-%02uT%02u:%02u:%02uZ</time></trkpt>\n",
                   breakSegment ? "</trkseg><trkseg>\n" : "", lat, lon, (double)ele, utc.year,
                   utc.month, utc.day, utc.hour, utc.minute, utc.second);
  return fit(n, cap);
}

void gpxFileName(char* out, size_t cap, const DateTime& t, uint8_t suffix) {
  if (suffix >= 2)
    snprintf(out, cap, "/tracks/%04u%02u%02u_%02u%02u%02u_%u.gpx", t.year, t.month, t.day, t.hour,
             t.minute, t.second, suffix);
  else
    snprintf(out, cap, "/tracks/%04u%02u%02u_%02u%02u%02u.gpx", t.year, t.month, t.day, t.hour,
             t.minute, t.second);
}
