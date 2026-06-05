#pragma once
#define _USE_MATH_DEFINES
#include <math.h>
#include <cmath>

class Coordinate
{
private:
    Sample x;
    Sample y;
    Sample z;

public:
    Coordinate();
    Coordinate(Sample xVal, Sample yVal, Sample zVal);

    Sample get(int dimension) const;
    void set(int dimension, Sample value);

    Sample distanceTo(const Coordinate& other) const;
    Sample angleToOnXZPlane(const Coordinate& other) const;

    Coordinate operator+(const Coordinate& other) const;
    Coordinate operator-(const Coordinate& other) const;
    Coordinate operator*(Sample scalar) const;
    Coordinate operator/(Sample scalar) const;
    bool operator==(const Coordinate& other) const;
};
