// curve.cpp
//
// Copyright (C) 2003, 2004 Jason Bevins
//
// This library is free software; you can redistribute it and/or modify it
// under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation; either version 2.1 of the License, or (at
// your option) any later version.
//
// This library is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
// License (COPYING.txt) for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this library; if not, write to the Free Software Foundation,
// Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//
// The developer's email is jlbezigvins@gmzigail.com (for great email, take
// off every 'zig'.)
//

#include "../interp.h"
#include "../misc.h"
#include "curve.h"

using namespace noise::module;

Curve::Curve ():
  Module (GetSourceModuleCount ()),
  m_pControlPoints (NULL)
{
  m_controlPointCount = 0;
}

Curve::~Curve ()
{
  delete[] m_pControlPoints;
}

void Curve::AddControlPoint (double inputValue, double outputValue)
{
  // Find the insertion point for the new control point and insert the new
  // point at that position.  The control point array will remain sorted by
  // input value.
  int insertionPos = FindInsertionPos (inputValue);
  InsertAtPos (insertionPos, inputValue, outputValue);
}

void Curve::ClearAllControlPoints ()
{
  delete[] m_pControlPoints;
  m_pControlPoints = NULL;
  m_controlPointCount = 0;
}

int Curve::FindInsertionPos (double inputValue)
{
  int insertionPos;
  for (insertionPos = 0; insertionPos < m_controlPointCount; insertionPos++) {
    if (inputValue < m_pControlPoints[insertionPos].inputValue) {
      // We found the array index in which to insert the new control point.
      // Exit now.
      break;
    } else if (inputValue == m_pControlPoints[insertionPos].inputValue) {
      // Each control point is required to contain a unique input value, so
      // throw an exception.
      throw noise::ExceptionInvalidParam ();
    }
  }
  return insertionPos;
}

double Curve::GetValue (double x, double y, double z) const
{
  assert (m_pSourceModule[0] != NULL);
  assert (m_controlPointCount >= 4);

  // Get the output value from the source module.
  double sourceModuleValue = m_pSourceModule[0]->GetValue (x, y, z);

  // Find the first element in the control point array that has an input value
  // larger than the output value from the source module.
  int indexPos;
  for (indexPos = 0; indexPos < m_controlPointCount; indexPos++) {
    if (sourceModuleValue < m_pControlPoints[indexPos].inputValue) {
      break;
    }
  }

  // Find the four nearest control points so that we can perform cubic
  // interpolation.
  int index0 = ClampValue (indexPos - 2, 0, m_controlPointCount - 1);
  int index1 = ClampValue (indexPos - 1, 0, m_controlPointCount - 1);
  int index2 = ClampValue (indexPos    , 0, m_controlPointCount - 1);
  int index3 = ClampValue (indexPos + 1, 0, m_controlPointCount - 1);

  // If some control points are missing (which occurs if the value from the
  // source module is greater than the largest input value or less than the
  // smallest input value of the control point array), get the corresponding
  // output value of the nearest control point and exit now.
  if (index1 == index2) {
    return m_pControlPoints[index1].outputValue;
  }
  
  // Compute the alpha value used for cubic interpolation.
  double input0 = m_pControlPoints[index1].inputValue;
  double input1 = m_pControlPoints[index2].inputValue;
  double alpha = (sourceModuleValue - input0) / (input1 - input0);

  // Now perform the cubic interpolation given the alpha value.
  return CubicInterp (
    m_pControlPoints[index0].outputValue,
    m_pControlPoints[index1].outputValue,
    m_pControlPoints[index2].outputValue,
    m_pControlPoints[index3].outputValue,
    alpha);
}

void Curve::InsertAtPos (int insertionPos, double inputValue,
  double outputValue)
{
  // Make room for the new control point at the specified position within the
  // control point array.  The position is determined by the input value of
  // the control point; the control points must be sorted by input value
  // within that array.
  ControlPoint* newControlPoints = new ControlPoint[m_controlPointCount + 1];
  for (int i = 0; i < m_controlPointCount; i++) {
    if (i < insertionPos) {
      newControlPoints[i] = m_pControlPoints[i];
    } else {
      newControlPoints[i + 1] = m_pControlPoints[i];
    }
  }
  delete[] m_pControlPoints;
  m_pControlPoints = newControlPoints;
  ++m_controlPointCount;

  // Now that we've made room for the new control point within the array, add
  // the new control point.
  m_pControlPoints[insertionPos].inputValue  = inputValue ;
  m_pControlPoints[insertionPos].outputValue = outputValue;
}
