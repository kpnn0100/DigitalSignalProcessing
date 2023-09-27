#pragma once
//include this to use full of Gyrus Space Libs
//base classes
#include "base/Block.h"
#include "base/SignalProcessor.h"

//Simple processor classes
#include "simpleProcessor/Delay.h"
#include "simpleProcessor/Gain.h"

//Spatial
#include "spatial/Positioner.h"
#include "spatial/PositionSimulator.h"
#include "spatial/RoomSimulator.h"

//Equalizer
#include "equalizer/LowPassFilter.h"
#include "equalizer/LowPassFilterThirdOrder.h"
#include "equalizer/LowPassFilterSixthOrder.h"
#include "equalizer/SchroederAllpass.h"

//Reverb
#include "reverb/Reverb.h"

#include "util/Util.h"