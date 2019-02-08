/***************************************************************************
 * libRSF - A Robust Sensor Fusion Library
 *
 * Copyright (C) 2018 Chair of Automation Technology / TU Chemnitz
 * For more information see https://www.tu-chemnitz.de/etit/proaut/self-tuning
 *
 * libRSF is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * libRSF is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with libRSF.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Tim Pfeifer (tim.pfeifer@etit.tu-chemnitz.de)
 ***************************************************************************/

#include "predictors/PredictOdometry.h"

namespace libRSF
{
  ceres::Vector PredictMotionDifferential2D(ceres::Vector Pose, double VelLeft, double VelRight, double DeltaTime)
  {
    ceres::Vector NewPose(3);

    NewPose << (VelLeft + VelRight) / 2, 0, (VelRight - VelLeft)/0.155;
    NewPose *= DeltaTime;

    NewPose.head(2) = RotationMatrix2D(Pose[2]) * NewPose.head(2);
    NewPose += Pose;

    NewPose[2] = NormalizeAngle(NewPose[2]);

    return NewPose;
  }
}
