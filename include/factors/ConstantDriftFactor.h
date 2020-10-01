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

/**
 * @file ConstantDriftFactor.h
 * @author Tim Pfeifer
 * @date 18.09.2018
 * @brief A Factor that connects two values with a constant drift model.
 * @copyright GNU Public License.
 *
 */

#ifndef CONSTANTDRIFTFACTOR_H
#define CONSTANTDRIFTFACTOR_H

#include "BaseFactor.h"
#include "../VectorMath.h"

namespace libRSF
{
  template <typename ErrorType, int Dim>
  class ConstantDriftFactorBase : public BaseFactor<ErrorType, false, true, Dim, Dim, Dim, Dim>
  {
    public:
      /** construct factor and store measurement */
      ConstantDriftFactorBase(ErrorType &Error, double DeltaTime)
      {
        this->_Error = Error;
        this->_DeltaTime = DeltaTime;
      }

      /** geometric error model */
      template <typename T>
      MatrixT<T, Dim * 2, 1> Evaluate(const T* const ValueOld, const T* const ValueNew,
                                            const T* const DriftOld, const T* const DriftNew,
                                            const double &DeltaTime) const
      {
        MatrixRefConst<T, Dim, 1> V1(ValueOld);
        MatrixRefConst<T, Dim, 1> V2(ValueNew);
        MatrixRefConst<T, Dim, 1> D1(DriftOld);
        MatrixRefConst<T, Dim, 1> D2(DriftNew);

        MatrixT<T, Dim * 2, 1> Error;

        Error.template head<Dim>() = V2 - V1 - (D1 * DeltaTime);
        Error.template tail<Dim>() = D2 - D1;

        return Error;
      }

      /** combine probabilistic and geometric model */
      template <typename T, typename... ParamsType>
      bool operator()(const T* const ValueOld, const T* const ValueNew,
                      const T* const DriftOld, const T* const DriftNew,
                      ParamsType... Params) const
      {
        return this->_Error.template weight<T>(this->Evaluate(ValueOld, ValueNew,
                                               DriftOld, DriftNew,
                                               this->_DeltaTime),
                                               Params...);
      }
  };

  /** compile time mapping from factor type enum to corresponding factor class */
  template<typename ErrorType>
  struct FactorTypeTranslator<FactorType::ConstDrift1, ErrorType> {using Type = ConstantDriftFactorBase<ErrorType, 1>;};
  template<typename ErrorType>
  struct FactorTypeTranslator<FactorType::ConstDrift2, ErrorType> {using Type = ConstantDriftFactorBase<ErrorType, 2>;};
  template<typename ErrorType>
  struct FactorTypeTranslator<FactorType::ConstDrift3, ErrorType> {using Type = ConstantDriftFactorBase<ErrorType, 3>;};
}
#endif // CONSTANTDRIFTFACTOR_H
