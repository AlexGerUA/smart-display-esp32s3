/***************************************************
Copyright (c) 2020 Luis Llamas
(www.luisllamas.es)

This program is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License as published by
the Free Software Foundation, either version 3 of the License, or (at your option) any later version. 

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public License along with this program.  If not, see <http://www.gnu.org/licenses 
****************************************************/

#include "Face.h"

// Surface for EyeDrawer (declared there as extern)
TFT_eSPI* g_drawSurface = nullptr;

Face::Face(TFT_eSPI* surface, uint16_t screenWidth, uint16_t screenHeight, uint16_t eyeSize)
	: LeftEye(*this), RightEye(*this), Blink(*this), Look(*this), Behavior(*this), Expression(*this),
	  _surface(surface) {
	Width = screenWidth;
	Height = screenHeight;
	EyeSize = eyeSize;
	CenterX = Width / 2;
	CenterY = Height / 2;
	LeftEye.IsMirrored = true;
	g_drawSurface = surface;
	DrawBottom = Height;
	Behavior.Clear();
	Behavior.Timer.Start();
}

void Face::LookFront() {
	Look.LookAt(0.0, 0.0);
}

void Face::LookRight() {
	Look.LookAt(-1.0, 0.0);
}

void Face::LookLeft() {
	Look.LookAt(1.0, 0.0);
}

void Face::LookTop() {
	Look.LookAt(0.0, 1.0);
}

void Face::LookBottom() {
	Look.LookAt(0.0, -1.0);
}

void Face::DoBlink() {
	Blink.Blink();
}

void Face::Update() {
	if(RandomBehavior) Behavior.Update();
	if(RandomLook) Look.Update();
	if(RandomBlink)	Blink.Update();
	Draw();
}

void Face::SetEmotion(eEmotions emotion) {
    CurrentEmotion = emotion;
}

void Face::Draw() {
  g_drawSurface = _surface;
  TFT_eSPI* drawSurface = _surface;
  drawSurface->fillRect(0, DrawTop, Width, DrawBottom - DrawTop, TFT_BLACK);   // face band only

  // Cat ears, as in ESP32-eyes-CAT
  int centerX = CenterX;  // from the face centre, not the screen
  int topY = CenterY - 80;  // from the eye centre
  int earOffsetX = EyeSize / 2 + 10;  // distance from centre, as in the original
  int earHeight = 25;
  int earWidth = 30;
  int tiltL = 0, tiltR = 0;

  // Ear shape depends on the emotion
  switch (CurrentEmotion) {
    case Angry:
      tiltL = -15; tiltR = 15; earHeight = 28; earWidth = 25; break;  // Pointed back
    case Furious:
      tiltL = -25; tiltR = 25; earHeight = 35; earWidth = 20; break;  // Little devil horns — very sharp
    case Happy:
    case Glee:
      tiltL = 12; tiltR = -12; earHeight = 20; earWidth = 35; break;  // Happily bent forward
    case Awe:
      tiltL = 5; tiltR = -5; earHeight = 30; earWidth = 28; break;    // Slightly raised in wonder
    case Sad:
      tiltL = 0; tiltR = 0; earHeight = 12; earWidth = 25; break;     // Drooping
    case Sleepy:
      tiltL = 0; tiltR = 0; earHeight = 8; earWidth = 30; break;      // Almost flat
    case Surprised:
      tiltL = 0; tiltR = 0; earHeight = 40; earWidth = 22; break;     // Very tall and narrow
    case Scared:
      tiltL = -8; tiltR = 8; earHeight = 35; earWidth = 18; break;    // Pressed back in fear
    case Worried:
      tiltL = -8; tiltR = 8; earHeight = 18; earWidth = 28; break;    // Slightly back and low
    case Focused:
      tiltL = 3; tiltR = -3; earHeight = 28; earWidth = 24; break;    // Slightly forward, focused
    case Suspicious:
      tiltL = -12; tiltR = 3; earHeight = 22; earWidth = 26; break;   // Asymmetric — one back
    case Annoyed:
      tiltL = -10; tiltR = 10; earHeight = 24; earWidth = 26; break;  // Irritated, back
    case Skeptic:
      tiltL = -5; tiltR = 8; earHeight = 25; earWidth = 28; break;    // One ear raised skeptically
    case Frustrated:
      tiltL = -18; tiltR = 18; earHeight = 26; earWidth = 24; break;  // Far back
    case Unimpressed:
      tiltL = 0; tiltR = 0; earHeight = 20; earWidth = 32; break;     // Indifferently flat
    case Squint:
      tiltL = -3; tiltR = 3; earHeight = 22; earWidth = 30; break;    // Slightly back from squinting
    default:
      tiltL = 0; tiltR = 0; earHeight = 25; earWidth = 30; break;     // Normal
  }

  // Eye offset (LeftEye is the reference, as in ESP32-eyes-CAT)
  float moveX = 0, moveY = 0;
  if (LeftEye.FinalConfig) {
      moveX = LeftEye.Transformation.Current.MoveX;
      moveY = LeftEye.Transformation.Current.MoveY;
  }

  // Scale the offset for the ears (0.7, as in the original)
  int earMoveX = (int)(moveX * 0.7);
  int earMoveY = (int)(-moveY * 0.7); // "-" because MoveY grows downward for eyes but ears move up

  // Left ear
  int lx = centerX - earOffsetX + earMoveX;
  int ly = topY + 10 + earMoveY;
  drawSurface->fillTriangle(
    lx - earWidth/2 + tiltL, ly + earHeight,
    lx + earWidth/2 + tiltL, ly + earHeight,
    lx, ly,
    TFT_WHITE
  );
  // Inner part of the left ear (pink), follows the shape
  int innerSizeL = earWidth / 5;  // Inner size follows ear width
  int innerOffsetL = earHeight / 4;  // Top inset follows ear height
  drawSurface->fillTriangle(
    lx - innerSizeL + tiltL, ly + earHeight - innerOffsetL,
    lx + innerSizeL + tiltL, ly + earHeight - innerOffsetL,
    lx, ly + innerOffsetL,
    TFT_PINK
  );

  // Right ear
  int rx = centerX + earOffsetX + earMoveX;
  int ry = topY + 10 + earMoveY;
  drawSurface->fillTriangle(
    rx - earWidth/2 + tiltR, ry + earHeight,
    rx + earWidth/2 + tiltR, ry + earHeight,
    rx, ry,
    TFT_WHITE
  );
  // Inner part of the right ear (pink), follows the shape
  int innerSizeR = earWidth / 5;  // Inner size follows ear width
  int innerOffsetR = earHeight / 4;  // Top inset follows ear height
  drawSurface->fillTriangle(
    rx - innerSizeR + tiltR, ry + earHeight - innerOffsetR,
    rx + innerSizeR + tiltR, ry + earHeight - innerOffsetR,
    rx, ry + innerOffsetR,
    TFT_PINK
  );
  
  // Draw left eye
	LeftEye.CenterX = CenterX - EyeSize / 2 - EyeInterDistance;
	LeftEye.CenterY = CenterY;
	LeftEye.Draw();
  
  // Draw right eye
	RightEye.CenterX = CenterX + EyeSize / 2 + EyeInterDistance;
	RightEye.CenterY = CenterY;
	RightEye.Draw();
	
	if (OnPush) OnPush();

}
