/***************************************************
Copyright (c) 2020 Luis Llamas
(www.luisllamas.es)

This program is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License as published by
the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public License along with this program.  If not, see <http://www.gnu.org/licenses
****************************************************/
// Cat-ears variant. Difference from upstream: no own sprite — draws into the
// given surface (the PSRAM frame buffer); OnPush sends the frame to the screen.

#ifndef _FACE_h
#define _FACE_h

#include <Arduino.h>
#include "Common.h"
#include "Animations.h"
#include "EyePresets.h"
#include "Eye.h"
#include "FaceExpression.h"
#include "FaceBehavior.h"
#include "LookAssistant.h"
#include "BlinkAssistant.h"
#include "FaceEmotions.hpp"
#include <functional>

class Face {

public:
    Face(TFT_eSPI* surface, uint16_t screenWidth, uint16_t screenHeight, uint16_t eyeSize);

    uint16_t Width;
    uint16_t Height;
    uint16_t CenterX;
    uint16_t CenterY;
    uint16_t EyeSize;
    uint16_t EyeInterDistance = 4;

    Eye LeftEye;
    Eye RightEye;
    BlinkAssistant Blink;
    LookAssistant Look;
    FaceBehavior Behavior;
    FaceExpression Expression;

    void Update();
    void DoBlink();

    bool RandomBehavior = true;
    bool RandomLook = true;
    bool RandomBlink = true;

    // Current emotion (ear shape)
    eEmotions CurrentEmotion = Normal;

    // Frame drawn into the surface — send it to the screen
    std::function<void()> OnPush = nullptr;
    // Screen band where the face can be (eyes with offsets + ears): only this
    // band is cleared and pushed, not the whole frame
    int16_t DrawTop = 0, DrawBottom = 0;

    void SetEmotion(eEmotions emotion);
    void LookLeft();
    void LookRight();
    void LookFront();
    void LookTop();
    void LookBottom();

protected:
    void Draw();
    TFT_eSPI* _surface;
};

#endif
