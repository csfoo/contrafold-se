//////////////////////////////////////////////////////////////////////
// InnerOptimizationWrapperStochasticGradient.hpp
//
// Inner optimization algorithm.
//////////////////////////////////////////////////////////////////////

#ifndef INNEROPTIMIZATIONWRAPPERSTOCHASTICGRADIENT_HPP
#define INNEROPTIMIZATIONWRAPPERSTOCHASTICGRADIENT_HPP

#include "OptimizationWrapper.hpp"

template<class RealT>
class StochasticGradient;

template<class RealT>
class OptimizationWrapper;

template<class RealT>
class InnerOptimizationWrapper;

//////////////////////////////////////////////////////////////////////
// class InnerOptimizationWrapperStochasticGradient
//////////////////////////////////////////////////////////////////////

template<class RealT>
class InnerOptimizationWrapperStochasticGradient : public InnerOptimizationWrapper<RealT>
{
    RealT log_base;
    int batch_size;
    int MAX_ITERATIONS;

    RealT s0; // stepsize = s0 / (1+iter)^s1
    RealT s1;

    RealT hyperparam_data;

public:
    InnerOptimizationWrapperStochasticGradient(OptimizationWrapper<RealT> *optimization_wrapper,
                                               const std::vector<int> &units,
                                               const std::vector<RealT> &C);

    RealT ComputeFunction(const std::vector<RealT> &x);
    void ComputeGradient(std::vector<RealT> &g, const std::vector<RealT> &x, const int batch_size);
    void Report(int iteration, const std::vector<RealT> &x, RealT step_size);
    void Report(const std::string &s);
    RealT Minimize(std::vector<RealT> &x0);

    int GetLogicalIndex(int i, int j, int k, int which_data);
    bool FindZerosInData(int i, int j, int which_data);
};

#include "InnerOptimizationWrapperStochasticGradient.ipp"

#endif
