#pragma once
// Console de teste pela Serial USB (até a UI existir).
// Comandos: ping | msg <texto> | pos <lat> <lon> | sos [<lat> <lon>] | cancel | vi | gps | status
void consolePoll();  // só lê comandos; os eventos do rádio são consumidos pela UI

struct RadioEvt;
void consolePrintEvent(const RadioEvt& e);  // imprime um evento do rádio (debug)
