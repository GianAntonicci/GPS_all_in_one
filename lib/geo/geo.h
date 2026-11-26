#pragma once
// Geometria na esfera (WGS84 aproximado por esfera de raio 6371 km).

double distanceM(double lat1, double lon1, double lat2, double lon2);   // haversine
double bearingDeg(double lat1, double lon1, double lat2, double lon2);  // rumo inicial em [0, 360)
double relativeBearingDeg(double targetDeg, double headingDeg);         // em [-180, 180)
// 0,0 é a convenção do protocolo para "posição desconhecida".
bool posKnown(double lat, double lon);
