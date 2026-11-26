#include "audio_cues.h"

CueOut AudioCues::update(const CueInput& in) {
  CueOut o{{Pattern::None, Pattern::None}, 0, false, false};
  auto add = [&](Pattern p) {
    if (o.n < 2) o.play[o.n++] = p;
  };

  const bool alarm = in.top == Overlay::PeerSos;
  if (alarm) add(Pattern::PeerAlarm);  // reafirmado a cada chamada: volta após um bipe de uma vez
  else if (alarm_) o.stopAlarm = true;
  alarm_ = alarm;
  o.alarm = alarm;

  if (in.counting && (!counting_ || in.countdownLeft != lastLeft_))
    add(in.countdownLeft <= 3 ? Pattern::CountdownFinal : Pattern::CountdownTick);
  counting_ = in.counting;
  lastLeft_ = in.counting ? in.countdownLeft : 0;

  if (in.mine != mine_) {
    if (in.mine == SosState::Received) add(Pattern::SosReceived);
    else if (in.mine == SosState::Seen) add(Pattern::SosSeen);
  }
  mine_ = in.mine;
  return o;
}

Pattern AudioCues::forGesture(Gesture g) {
  switch (g) {
    case Gesture::Click: return Pattern::Click;
    case Gesture::Hold2s: return Pattern::Hold;
    default: return Pattern::None;
  }
}
