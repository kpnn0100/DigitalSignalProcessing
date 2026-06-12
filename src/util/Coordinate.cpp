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

Coordinate::Coordinate(Sample xVal, Sample yVal, Sample zVal)
    : x(xVal), y(yVal), z(zVal)
{
}

Sample Coordinate::get(int dimension) const
{
    switch (dimension)
    {
    case 0: return x;
    case 1: return y;
    case 2: return z;
    default: return 0.0; // Invalid dimension, return default
    }
}

void Coordinate::set(int dimension, Sample value)
{
    switch (dimension)
    {
    case 0: x = value; break;
    case 1: y = value; break;
    case 2: z = value; break;
    default: break; // Invalid dimension, do nothing
    }
}

Sample Coordinate::distanceTo(const Coordinate& other) const
{
    Sample dx = x - other.x;
    Sample dy = y - other.y;
    Sample dz = z - other.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}
Sample Coordinate::angleToOnXZPlane(const Coordinate& other) const
{
    Sample xVector = other.x - x;
    Sample yVector = other.z - z;
    if (xVector == 0.0 && yVector == 0)
    {
        return 0;
    }
    Sample degree = atan2(yVector, xVector) * 180 / M_PI;
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

Coordinate Coordinate::operator*(Sample scalar) const
{
    return Coordinate(x * scalar, y * scalar, z * scalar);
}

Coordinate Coordinate::operator/(Sample scalar) const
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