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

#include "FactorGraph.h"


namespace libRSF
{
  void StateList::add(string Type, double Timestamp, int Number)
  {
    this->add(StateID(Type, Timestamp, Number));
  }

  void StateList::add(StateID State)
  {
    _List.emplace_back(State);
  }

  void StateList::clear()
  {
    _List.clear();
  }

  FactorGraph::FactorGraph() : _Graph(this->_DefaultProblemOptions), _Structure(&_Graph, &_StateData), _SolverDuration(0.0), _SolverIterations(0), _MarginalizationDuration(0.0)
  {}

  void FactorGraph::solve()
  {
    /** check if config is valid */
    std::string OptionsError;
    if (_SolverOptions.IsValid(&OptionsError) == false)
    {
      PRINT_ERROR("The given solver options are wrong: ", OptionsError);
    }
    else
    {
      /** call ceres to solve the optimization problem */
      ceres::Solve(_SolverOptions, &_Graph, &_Report);
      _SolverDuration += _Report.total_time_in_seconds;
      _SolverIterations += _Report.num_successful_steps + _Report.num_unsuccessful_steps;
    }
  }

  void FactorGraph::solve(ceres::Solver::Options Options)
  {
    _SolverOptions = Options;
    this->solve();
  }

  void FactorGraph::addState(string Name, StateType Type, double Timestamp)
  {
    StateData Element(Type, Timestamp);
    addState(Name, Element);
  }

  void FactorGraph::addState(string Name, StateData &Element)
  {
    _StateData.addElement(Name, Element);
    double Timestamp = Element.getTimestamp();
    int StateNumber = _StateData.countElement(Name, Timestamp) - 1;

    double* StatePointer = _StateData.getElement(Name, Timestamp, StateNumber).getMeanPointer();
    int StateSize = _StateData.getElement(Name, Timestamp, StateNumber).getMean().size();

    /** add state vector as parameter block with local parametrization if required */
    switch (_StateData.getElement(Name, Timestamp, StateNumber).getType())
    {
      case StateType::Angle:
        _Graph.AddParameterBlock(StatePointer, StateSize, AngleLocalParameterization::Create());
        break;

      case StateType::Quat:
        {
          /** initialize with valid quaternion */
          Vector4 Quaternion;
          Quaternion << 0, 0, 0, 1; /**< x,y,z,w */
          _StateData.getElement(Name, Timestamp, StateNumber).setMean(Quaternion);

          _Graph.AddParameterBlock(StatePointer, StateSize, QuaternionLocalParameterization::Create());
          break;
        }

      case StateType::Pose2:
        {
          ceres::LocalParameterization *LocalParamPose2 = new ceres::ProductParameterization(new ceres::IdentityParameterization(2), AngleLocalParameterization::Create());
          _Graph.AddParameterBlock(StatePointer, StateSize, LocalParamPose2);
          break;
        }

      case StateType::Pose3:
        {
          /** initialize with valid quaternion */
          Vector7 Pose3;
          Pose3 << 0,0,0, 0,0,0,1;
          _StateData.getElement(Name, Timestamp, StateNumber).setMean(Pose3);

          ceres::LocalParameterization *LocalParamPose3 = new ceres::ProductParameterization(new ceres::IdentityParameterization(3), QuaternionLocalParameterization::Create());
          _Graph.AddParameterBlock(StatePointer, StateSize, LocalParamPose3);
          break;
        }

      case StateType::Switch:
        {
          /** init with one */
          _StateData.getElement(Name, Timestamp, StateNumber).setMean(Vector1::Identity());

          _Graph.AddParameterBlock(StatePointer, StateSize);

          /** limit between zero and one */
          _Graph.SetParameterLowerBound(_StateData.getElement(Name, Timestamp, StateNumber).getMeanPointer(), 0, 0.0);
          _Graph.SetParameterUpperBound(_StateData.getElement(Name, Timestamp, StateNumber).getMeanPointer(), 0, 1.0);
          break;
        }

      default:
        _Graph.AddParameterBlock(StatePointer, StateSize);
        break;
    }
  }

  void FactorGraph::addStateWithCheck(string Name, StateType Type, double Timestamp)
  {
    if (!this->getStateData().checkElement(Name, Timestamp))
    {
      this->addState(Name, Type, Timestamp);
    }
  }

  void FactorGraph::addIMUPreintegrationFactor(StateList List, PreintegratedIMUResult IMUState)
  {
    /** create noise model */
    GaussianFull<15> IMUNoiseModel;
    IMUNoiseModel.setCovarianceMatrix(IMUState.PreIntCov);

    /** add to factor graph */
    typedef typename FactorTypeTranslator<FactorType::IMUPretintegration, GaussianFull<15>>::Type FactorClassType;
    addFactorGeneric<GaussianFull<15>, FactorClassType> (IMUNoiseModel,
        List._List,
        FactorType::IMUPretintegration,
        nullptr,
        List._List.front().Timestamp,
        IMUState);
  }

  void FactorGraph::printReport() const
  {
    std::cout << _Report.FullReport() << "\n";
  }

  ceres::Solver::Summary FactorGraph::getSolverSummary() const
  {
    return _Report;
  }

  void FactorGraph::setConstant(string Name, double Timestamp)
  {
    for (int StateNumber = _StateData.countElement(Name, Timestamp); StateNumber > 0; --StateNumber)
    {
      _Graph.SetParameterBlockConstant(_StateData.getElement(Name, Timestamp, StateNumber - 1).getMeanPointer());
    }
  }

  void FactorGraph::setVariable(string Name, double Timestamp)
  {
    for (int StateNumber = _StateData.countElement(Name, Timestamp); StateNumber > 0; --StateNumber)
    {
      _Graph.SetParameterBlockVariable(_StateData.getElement(Name, Timestamp, StateNumber - 1).getMeanPointer());
    }
  }

  void FactorGraph::setSubsetConstant(string Name, double Timestamp, int Number, const std::vector<int> &ConstantIndex)
  {
    _Graph.SetParameterization(_StateData.getElement(Name, Timestamp, Number).getMeanPointer(),
                               new ceres::SubsetParameterization(_StateData.getElement(Name, Timestamp, Number).getMean().size(), ConstantIndex));
  }

  void FactorGraph::setUpperBound(const string &Name, const double Timestamp, const int StateNumber, const Vector &Bound)
  {
    const int Dim = _StateData.getElement(Name, Timestamp, StateNumber).getMean().size();

    if (Bound.size() == Dim)
    {
      for (int n = 0; n < Dim; n++)
      {
        _Graph.SetParameterUpperBound(_StateData.getElement(Name, Timestamp, StateNumber).getMeanPointer(), n, Bound(n));
      }
    }
    else
    {
      PRINT_ERROR("Dimension of bounding vector is ", Bound.size(), " instead of ", Dim, "!") ;
    }
  }

  void FactorGraph::setLowerBound(const string &Name, const double Timestamp, const int StateNumber, const Vector &Bound)
  {
    const int Dim = _StateData.getElement(Name, Timestamp, StateNumber).getMean().size();

    if (Bound.size() == Dim)
    {
      for (int n = 0; n < Dim; n++)
      {
        _Graph.SetParameterLowerBound(_StateData.getElement(Name, Timestamp, StateNumber).getMeanPointer(), n, Bound(n));
      }
    }
    else
    {
      PRINT_ERROR("Dimension of bounding vector is ", Bound.size(), " instead of ", Dim, "!") ;
    }
  }

  bool FactorGraph::marginalizeStates(std::vector<StateID> States)
  {
    /** time measurement */
    Timer MargTimer;

    /**check if state vector has been filled */
    if (States.empty() == true)
    {
      PRINT_ERROR("Request for marginalization without any state.");
      return false;
    }

    std::vector<double*> MarginalStates;
    int MarginalSize = 0;
    for (const StateID &State : States)
    {
      /** check if this is a set of valid states */
      if (this->_StateData.checkElement(State.ID, State.Timestamp, State.Number) == false)
      {
        PRINT_ERROR("State doesn't exist at: ", State.Timestamp, " Type: ", State.ID, " Number: ", State.Number);
        return false;
      }

      /** store pointer */
      MarginalStates.push_back(_StateData.getElement(State.ID, State.Timestamp, State.Number).getMeanPointer());

      /** compute size of the marginalized system */
      MarginalSize += _Graph.ParameterBlockLocalSize(MarginalStates.back());
    }

    /** get connected states */
    std::vector<double*> ConnectedStates;
    std::vector<ceres::ResidualBlockId> Factors;
    std::vector<int> GlobalSize;
    std::vector<int> LocalSize;
    std::vector<StateID> StateIDs;
    std::vector<StateType> StateTypes;
    _Structure.getMarginalizationInfo(MarginalStates, ConnectedStates, Factors, GlobalSize, LocalSize, StateIDs, StateTypes);

    if (ConnectedStates.empty() == false)
    {
      /** calculate mean and uncertainty*/
      ceres::Problem::EvaluateOptions Options;
      Options.apply_loss_function = true;
      Options.num_threads = std::thread::hardware_concurrency();

      /** set which states should be evaluated (base + the connected ones) */
      std::vector<double*> CombinedStates;
      CombinedStates.insert(CombinedStates.end(), MarginalStates.begin(), MarginalStates.end());
      CombinedStates.insert(CombinedStates.end(), ConnectedStates.begin(), ConnectedStates.end());
      Options.parameter_blocks = CombinedStates;

      /** set which factors should be evaluated */
      Options.residual_blocks = Factors;

      /** evaluate sub-problem */
      ceres::CRSMatrix JacobianCRS;
      std::vector<double> ResidualVec;
      _Graph.Evaluate(Options, nullptr, &ResidualVec, nullptr, &JacobianCRS);

      /** map error to vector */
      Vector Residuals = Eigen::Map<Vector, Eigen::Unaligned>(ResidualVec.data(), ResidualVec.size());

      /** convert into eigen matrix */
      Matrix Jacobian;
      CRSToMatrix(JacobianCRS, Jacobian);

      /** compute marginalization */
      Matrix JacobianMarg;
      Vector ResidualMarg;
      Marginalize(Residuals, Jacobian, ResidualMarg, JacobianMarg, MarginalSize);

      /** store original states */
      std::vector<Vector> OriginalStates;
      for (int n = 0; n < static_cast<int>(ConnectedStates.size()); n++)
      {
        VectorRef<double, Dynamic> State(ConnectedStates.at(n), GlobalSize.at(n));
        OriginalStates.push_back(State);
      }

      /** add factor */
      ceres::ResidualBlockId ID = _Graph.AddResidualBlock(new MarginalPrior(LocalSize,
                                  GlobalSize,
                                  OriginalStates,
                                  StateTypes,
                                  JacobianMarg,
                                  ResidualMarg),
                                  nullptr,
                                  ConnectedStates);

      /** add factor to internal structure */
      _Structure.addFactor<ErrorModel<0, 0>>(FactorType::Marginal, StateIDs.front().Timestamp, ID, nullptr, StateIDs, ConnectedStates, StateTypes);

      _MarginalizationDuration += MargTimer.getSeconds();
    }
    else
    {
      PRINT_WARNING("No connected states in marginalization. Marginalized states get deleted directly!");
    }

    /** remove marginalized states in reverse order */
    std::reverse(States.begin(), States.end());
    for (const StateID &State : States)
    {
      this->removeState(State.ID, State.Timestamp, State.Number);
    }

    return true;
  }

  bool FactorGraph::marginalizeState(const string Name, const double Timestamp, const int Number)
  {
    std::vector<StateID> SingleState;
    SingleState.emplace_back(StateID(Name, Timestamp, Number));

    return this->marginalizeStates(SingleState);
  }

  bool FactorGraph::marginalizeAllStatesOutsideWindow(double TimeWindow, double CurrentTime)
  {
    /** calculate time boarder */
    double CutTime = CurrentTime - TimeWindow;

    /** check if marginalization is required before doing anything  */
    double TimeFirst = CutTime;
    _StateData.getTimeFirstOverall(TimeFirst);
    if (TimeFirst > CutTime)
    {
      /** no marginalization required, exit here... */
      return true;
    }

    /** collect relevant states */
    std::vector<StateID> States;

    /** iterate over state names */
    std::vector<string> StateNames = _StateData.getKeysAll();
    for (const string &Name : StateNames)
    {
      /** check if there is an element below cut time */
      double TimeFirstState;
      if (_StateData.getTimeFirst(Name, TimeFirstState))
      {
        if (TimeFirstState <= CutTime)
        {
          /** iterate over timestamps */
          std::vector<double> Times;
          if (_StateData.getTimesBelowOrEqual(Name, CutTime, Times))
          {
            for (const double Time : Times)
            {
              /** iterate over number */
              int Numbers = _StateData.countElement(Name, Time);
              for (int n = 0; n < Numbers; n++)
              {
                States.emplace_back(StateID(Name, Time, n));
              }
            }
          }
        }
      }
    }

    /** marginalize */
    return this->marginalizeStates(States);
  }

  bool FactorGraph::computeCovariance(const string Name, const double Timestamp)
  {
    return CalculateCovariance(_Graph, _StateData, Name, Timestamp);
  }

  bool FactorGraph::computeCovariance(const string Name)
  {
    return CalculateCovariance(_Graph, _StateData, Name);
  }

  bool FactorGraph::computeCovarianceSigmaPoints(const string Name, const double Timestamp, const int StateNumber)
  {
    switch (_StateData.getElement(Name, Timestamp, StateNumber).getMean().size())
    {
      case 1:
        return EstimateCovarianceSigmaPoint<1>(_Graph, _StateData, Name, Timestamp, StateNumber);
        break;

      case 2:
        return EstimateCovarianceSigmaPoint<2>(_Graph, _StateData, Name, Timestamp, StateNumber);
        break;

      case 3:
        return EstimateCovarianceSigmaPoint<3>(_Graph, _StateData, Name, Timestamp, StateNumber);
        break;

      case 4:
        return EstimateCovarianceSigmaPoint<4>(_Graph, _StateData, Name, Timestamp, StateNumber);
        break;

      default:
        PRINT_ERROR("Sigma Point Covariance for ",
                    _StateData.getElement(Name, Timestamp, StateNumber).getMean().size(),
                    " dimensions is missing. Add case!");
        break;
    }
    return false;
  }

  void FactorGraph::sampleCost1D(const string StateName,
                                 const double Timestamp,
                                 const int Number,
                                 const int PointCount,
                                 const double Range,
                                 StateDataSet &Result)
  {
    EvaluateCostSurface<1>(_Graph, _StateData.getElement(StateName, Timestamp, Number).getMeanPointer(), PointCount, Range, Result);
  }

  void FactorGraph::sampleCost2D(const string StateName,
                                 const double Timestamp,
                                 const int Number,
                                 const int PointCount,
                                 const double Range,
                                 StateDataSet &Result)
  {
    EvaluateCostSurface<2>(_Graph, _StateData.getElement(StateName, Timestamp, Number).getMeanPointer(), PointCount, Range, Result);
  }

  StateDataSet &FactorGraph::getStateData()
  {
    return _StateData;
  }

  void FactorGraph::removeState(string Name, double Timestamp, int Number)
  {
    /** safety check */
    if (_StateData.checkElement(Name, Timestamp, Number))
    {
      /** remove from internal lists*/
      StateID State(Name, Timestamp, Number);
      _Structure.removeState(State);

      /** remove from ceres */
      _Graph.RemoveParameterBlock(_StateData.getElement(Name, Timestamp, Number).getMeanPointer());

      /** remove from our StateDataSet */
      _StateData.removeElement(Name, Timestamp, Number);
    }
    else
    {
      PRINT_ERROR("State doesn't exist at: ", Timestamp, " Type: ", Name, " Number: ", Number);
    }
  }

  void FactorGraph::removeState(string Name, double Timestamp)
  {
    if (_StateData.checkElement(Name, Timestamp))
    {
      /** remove from ceres::problem */
      for (int StateNumber = _StateData.countElement(Name, Timestamp); StateNumber > 0; --StateNumber)
      {
        /** remove from internal lists*/
        StateID State(Name, Timestamp, StateNumber - 1);
        _Structure.removeState(State);

        /** remove from ceres */
        _Graph.RemoveParameterBlock(_StateData.getElement(Name, Timestamp, StateNumber - 1).getMeanPointer());

        /** remove from our StateDataSet */
        _StateData.removeElement(Name, Timestamp, StateNumber - 1);
      }
    }
    else
    {
      PRINT_ERROR("State doesn't exist at: ", Timestamp, " Type: ", Name);
    }
  }

  void FactorGraph::removeStatesOutsideWindow(string Name, double TimeWindow, double CurrentTime)
  {
    double CutTime = CurrentTime - TimeWindow;
    double Timestamp;
    bool TimestampExists;

    /** loop over timestamps */
    TimestampExists = _StateData.getTimeFirst(Name, Timestamp);
    if (TimestampExists)
    {
      while ((Timestamp <= CutTime) && TimestampExists)
      {
        double TimestampTemp = Timestamp;
        TimestampExists = _StateData.getTimeNext(Name, Timestamp, Timestamp);
        removeState(Name, TimestampTemp);
      }
    }
    else
    {
      PRINT_ERROR("State Type doesn't exist: ", Name);
    }
  }

  void FactorGraph::removeAllStatesOutsideWindow(double TimeWindow, double CurrentTime)
  {
    for (auto const &State : _StateData)
    {
      removeStatesOutsideWindow(State.first, TimeWindow, CurrentTime);
    }
  }

  void FactorGraph::removeFactor(FactorType CurrentFactorType, double Timestamp)
  {
    if (_Structure.checkFactor(CurrentFactorType, Timestamp))
    {
      /** loop over factors */
      ceres::ResidualBlockId CeresID;
      for (int FactorNumber = _Structure.countFactor(CurrentFactorType, Timestamp); FactorNumber > 0; FactorNumber--)
      {
        FactorID Factor(CurrentFactorType, Timestamp, FactorNumber - 1);
        _Structure.getResidualID(Factor, CeresID);

        /** remove factor in ceres */
        _Graph.RemoveResidualBlock(CeresID);

        /** remove factor in libRSF */
        _Structure.removeFactor(Factor);
      }
    }
    else
    {
      PRINT_ERROR("Factor doesn't exist: ", CurrentFactorType, " at ", Timestamp);
    }
  }

  void FactorGraph::removeFactorsOutsideWindow(FactorType CurrentFactorType, double TimeWindow, double CurrentTime)
  {
    double FirstTime;
    _Structure.getTimeFirst(CurrentFactorType, FirstTime);
    if (CurrentTime - TimeWindow >= FirstTime)
    {
      /** get relevant Timestamps */
      std::vector<double> Timestamps;
      _Structure.getTimesBelow(CurrentFactorType, CurrentTime - TimeWindow, Timestamps);

      /** remove all */
      for (const double &Time : Timestamps)
      {
        this->removeFactor(CurrentFactorType, Time);
      }
    }
  }

  void FactorGraph::removeAllFactorsOutsideWindow(double TimeWindow, double CurrentTime)
  {
    std::vector<FactorType> Factors;
    _Structure.getFactorTypes(Factors);
    for (auto const &Factor : Factors)
    {
      removeFactorsOutsideWindow(Factor, TimeWindow, CurrentTime);
    }
  }

  void FactorGraph::setConstantOutsideWindow(string Name, double TimeWindow, double CurrentTime)
  {
    double CutTime = CurrentTime - TimeWindow;
    double Timestamp;
    bool TimestampExists;

    /** loop over timestamps that are older than TimeWindow + 1 seconds */
    TimestampExists = _StateData.getTimeBelow(Name, CurrentTime - TimeWindow + 1.0, Timestamp);
    if (TimestampExists)
    {
      while ((Timestamp <= CutTime) && TimestampExists)
      {
        double TimestampTemp = Timestamp;
        TimestampExists = _StateData.getTimeNext(Name, Timestamp, Timestamp);
        setConstant(Name, TimestampTemp);
      }
    }
    else if (!_StateData.checkID(Name))
    {
      PRINT_ERROR("State doesn't exist: ", Name);
    }
  }

  void FactorGraph::setAllConstantOutsideWindow(double TimeWindow, double CurrentTime)
  {
    for (auto const &State : _StateData)
    {
      setConstantOutsideWindow(State.first, TimeWindow, CurrentTime);
    }
  }

  double FactorGraph::getSolverDurationAndReset()
  {
    /** reset solver duration before value is returned */
    const double Duration = _SolverDuration;
    _SolverDuration = 0.0;
    return Duration;
  }

  double FactorGraph::getMarginalDurationAndReset()
  {
    /** reset marginalization duration before value is returned */
    const double Duration = _MarginalizationDuration;
    _MarginalizationDuration = 0.0;
    return Duration;
  }

  int FactorGraph::getSolverIterationsAndReset()
  {
    /** reset marginalization duration before value is returned */
    const int Iterarions = _SolverIterations;
    _SolverIterations = 0;
    return Iterarions;
  }

  void FactorGraph::enableErrorModel(FactorType CurrentFactorType)
  {
    /** loop over all models */
    std::vector<ErrorModelBase*> ErrorModels;
    _Structure.getErrorModels(CurrentFactorType, ErrorModels);
    for (const auto &ErrorModel : ErrorModels)
    {
      ErrorModel->enable();
    }
  }

  void FactorGraph::disableErrorModel(FactorType CurrentFactorType)
  {
    std::vector<ErrorModelBase*> ErrorModels;
    _Structure.getErrorModels(CurrentFactorType, ErrorModels);
    for (const auto &ErrorModel : ErrorModels)
    {
      ErrorModel->disable();
    }
  }

  void FactorGraph::enableErrorModels()
  {
    std::vector<FactorType> Factors;
    _Structure.getFactorTypes(Factors);
    for (const auto &Factor : Factors)
    {
      this->enableErrorModel(Factor);
    }
  }

  void FactorGraph::disableErrorModels()
  {
    std::vector<FactorType> Factors;
    _Structure.getFactorTypes(Factors);
    for (const auto &Factor : Factors)
    {
      this->disableErrorModel(Factor);
    }
  }

  void FactorGraph::getFactorsOfState(const string Name, const double Timestamp, const int Number, std::vector<FactorID> &Factors) const
  {
    StateID State(Name, Timestamp, Number);
    _Structure.getFactorsOfState(State, Factors);
  }

  int FactorGraph::countFactorsOfType(const FactorType CurrentFactorType) const
  {
    return _Structure.countFactorType(CurrentFactorType);
  }

  void FactorGraph::computeUnweightedError(const FactorType CurrentFactorType, std::vector<double> &ErrorData)
  {
    disableErrorModel(CurrentFactorType);

    /** get residual IDs */
    std::vector<ceres::ResidualBlockId> IDs;
    _Structure.getResidualIDs(CurrentFactorType, IDs);

    /** check IDs */
    std::vector<ceres::ResidualBlockId> IDsCeres;
    std::vector<bool> Existence;
    _Graph.GetResidualBlocks(&IDsCeres);
    for (auto const &ID1 : IDs)
    {
      bool Exists = false;
      for (auto const &ID2 : IDsCeres)
      {
        if (ID1 == ID2)
        {
          Exists = true;
        }
      }
      Existence.push_back(Exists);
      if (Exists == false)
      {
        PRINT_ERROR("Found missing ID");
      }
    }

    /** configure evaluation */
    ceres::Problem::EvaluateOptions Options;
    Options.num_threads = std::thread::hardware_concurrency();
    Options.residual_blocks = IDs;
    Options.apply_loss_function = false;

    /** compute the errors */
    _Graph.Evaluate(Options, nullptr, &ErrorData, nullptr, nullptr);

    /** re-enable error model */
    enableErrorModel(CurrentFactorType);

    /** create factor ID */
    double TimeFirst;
    _Structure.getTimeFirst(CurrentFactorType, TimeFirst);
    FactorID FirstID(CurrentFactorType, TimeFirst, 0);

    /** remove unused dimensions */
    int InputSize, OutputSize;
    _Structure.getErrorInputSize(FirstID, InputSize);
    _Structure.getErrorOutputSize(FirstID, OutputSize);

    for (int n = ErrorData.size() - 1; n > 0; n -= OutputSize)
    {
      for (int m = 0 ; m < (OutputSize - InputSize); m++)
      {
        ErrorData.erase(ErrorData.begin() + n - m);
      }
    }
  }

  void FactorGraph::computeUnweightedError(const FactorType CurrentFactorType, const double Time, const int Number, Vector &Error)
  {
    /** disable error model */
    disableErrorModel(CurrentFactorType);

    /** check */
    if (_Structure.checkFactor(CurrentFactorType, Time, Number) == false)
    {
      PRINT_ERROR("Factor does not exist: ", CurrentFactorType, " ", Time, " ", Number);
      return;
    }

    /** get internal ceres ID */
    ceres::ResidualBlockId CeresID;
    FactorID OurID(CurrentFactorType, Time, Number);
    _Structure.getResidualID(OurID, CeresID);

    std::vector<ceres::ResidualBlockId> CeresIDs;
    CeresIDs.push_back(CeresID);

    /** configure evaluation */
    ceres::Problem::EvaluateOptions Options;
    Options.num_threads = 1;
    Options.residual_blocks = CeresIDs;
    Options.apply_loss_function = false;

    /** compute error */
    std::vector<double> ErrorData;
    _Graph.Evaluate(Options, nullptr, &ErrorData, nullptr, nullptr);

    /** re-enable error model again */
    enableErrorModel(CurrentFactorType);

    /** copy to eigen vector */
    int Size;
    _Structure.getErrorInputSize(OurID, Size);
    Error.resize(Size);
    for (int n = 0; n < Size; n++)
    {
      Error(n) = ErrorData.at(n);
    }
  }

}
