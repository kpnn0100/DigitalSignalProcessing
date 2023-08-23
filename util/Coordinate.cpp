/*
  ==============================================================================

    Coordinate.cpp
    Created: 7 Aug 2023 9:01:16pm
    Author:  PC

  ==============================================================================
*/

#include "Coordinate.h"
#include <stdexcept>

Coordinate::Coordinate()
    : x(0.0), y(0.0), z(0.0)
{
}

Coordinate::Coordinate(double xVal, double yVal, double zVal)
    : x(xVal), y(yVal), z(zVal)
{
}

double Coordinate::get(int dimension) const
{
    switch (dimension)
    {
    case 0: return x;
    case 1: return y;
    case 2: return z;
    default: return 0.0; // Invalid dimension, return default
    }
}

void Coordinate::set(int dimension, double value)
{
    switch (dimension)
    {
    case 0: x = value; break;
    case 1: y = value; break;
    case 2: z = value; break;
    default: break; // Invalid dimension, do nothing
    }
}

double Coordinate::distanceTo(const Coordinate& other) const
{
    double dx = x - other.x;
    double dy = y - other.y;
    double dz = z - other.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}
double Coordinate::angleToOnXZPlane(const Coordinate& other) const
{
    double xVector = other.x - x;
    double yVector = other.z - z;
    if (xVector == 0.0 && yVector == 0)
    {
        return 0;
    }
    double degree = atan2(yVector, xVector) * 180 / M_PI;
    if (degree < 0.0)
        degree = 360.0 + degree;
    return degree;
}


Coordinate Coordinate::operator+(const Coordinate& other) const
{
    return Coordinate(x + other.x, y + other.y, z + other.z);
}

Coordinate Coordinate::operator-(const Coordinate& other) const
{
    return Coordinate(x - other.x, y - other.y, z - other.z);
}

Coordinate Coordinate::operator*(double scalar) const
{
    return Coordinate(x * scalar, y * scalar, z * scalar);
}

Coordinate Coordinate::operator/(double scalar) const
{
    if (scalar != 0.0)
        return Coordinate(x / scalar, y / scalar, z / scalar);
    else
        throw std::runtime_error("Division by zero!");
}
bool Coordinate::operator==(const Coordinate& other) const
{
    return x == other.x && y == other.y && z == other.z;
}