#pragma once
#define _USE_MATH_DEFINES
#include <math.h>
#include <cmath>

class Coordinate
{
private:
    double x;
    double y;
    double z;

public:
    Coordinate();
    Coordinate(double xVal, double yVal, double zVal);

    double get(int dimension) const;
    void set(int dimension, double value);

    double distanceTo(const Coordinate& other) const;
    double angleToOnXZPlane(const Coordinate& other) const;

    Coordinate operator+(const Coordinate& other) const;
    Coordinate operator-(const Coordinate& other) const;
    Coordinate operator*(double scalar) const;
    Coordinate operator/(double scalar) const;
    bool operator==(const Coordinate& other) const;
};
