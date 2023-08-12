#pragma once
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

    Coordinate operator+(const Coordinate& other) const;
    Coordinate operator-(const Coordinate& other) const;
    Coordinate operator*(double scalar) const;
    Coordinate operator/(double scalar) const;
};
