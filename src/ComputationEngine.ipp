//////////////////////////////////////////////////////////////////////
// ComputationEngine.cpp
//////////////////////////////////////////////////////////////////////

#include "ComputationEngine.hpp"

//////////////////////////////////////////////////////////////////////
// ComputationEngine::ComputationEngine()
// ComputationEngine::~ComputationEngine()
//
// Constructor and destructor.
//////////////////////////////////////////////////////////////////////

template<class RealT>
ComputationEngine<RealT>::ComputationEngine(const Options &options,
                                            const std::vector<FileDescription> &descriptions,
                                            InferenceEngine<RealT> &inference_engine,
                                            ParameterManager<RealT> &parameter_manager) :
    DistributedComputation<RealT, SharedInfo<RealT>, NonSharedInfo>(options.GetBoolValue("verbose_output")),
    options(options),
    descriptions(descriptions),
    inference_engine(inference_engine),
    parameter_manager(parameter_manager)
{ }

template<class RealT>
ComputationEngine<RealT>::~ComputationEngine()
{}

//////////////////////////////////////////////////////////////////////
// ComputationEngine::DoComputation()
//
// Decide what type of computation needs to be done and then
// pass the work on to the appropriate routine.
//////////////////////////////////////////////////////////////////////

template<class RealT>
void ComputationEngine<RealT>::DoComputation(std::vector<RealT> &result, 
                                             const SharedInfo<RealT> &shared,
                                             const NonSharedInfo &nonshared)
{
    switch (shared.command)
    {
        case CHECK_PARSABILITY:
            CheckParsability(result, nonshared);
            break;
        case COMPUTE_SOLUTION_NORM_BOUND:
            ComputeSolutionNormBound(result, shared, nonshared);
            break;
        case COMPUTE_GRADIENT_NORM_BOUND:
            ComputeGradientNormBound(result, nonshared);
            break;
        case COMPUTE_LOSS:
            ComputeLoss(result, shared, nonshared);
            break;
        case COMPUTE_FUNCTION:
            ComputeFunctionAndGradient(result, shared, nonshared, false);
            break;
        case COMPUTE_GRADIENT:
            ComputeFunctionAndGradient(result, shared, nonshared, true);
            break;
        case COMPUTE_MSTEP_FUNCTION:
            ComputeMStepFunctionAndGradient(result, shared, nonshared, false);
            break;
        case COMPUTE_MSTEP_GRADIENT:
            ComputeMStepFunctionAndGradient(result, shared, nonshared, true);
            break;
        case COMPUTE_GAMMAMLE_FUNCTION:
            ComputeGammaMLEFunctionAndGradient(result, shared, nonshared, false);
            break;
        case COMPUTE_GAMMAMLE_GRADIENT:
            ComputeGammaMLEFunctionAndGradient(result, shared, nonshared, true);
            break;
        case COMPUTE_GAMMAMLE_SCALING_FACTOR:
            ComputeGammaMLEScalingFactor(result, shared, nonshared);
            break;
        case CHECK_ZEROS_IN_DATA:
            CheckZerosInData(result, shared, nonshared);
            break;
        case COMPUTE_FUNCTION_SE:
            ComputeFunctionAndGradientSE(result, shared, nonshared, false);
            break;
        case COMPUTE_GRADIENT_SE:
            ComputeFunctionAndGradientSE(result, shared, nonshared, true);
            break;
        case COMPUTE_HV:
            ComputeHessianVectorProduct(result, shared, nonshared);
            break;
        case PREDICT:
            Predict(result, shared, nonshared);
            break;
        default: 
            Assert(false, "Unknown command type.");
            break;
    }
}

//////////////////////////////////////////////////////////////////////
// ComputationEngine::CheckParsability()
//
// Check to see if a sequence is parsable or not.  Return a
// vector with a "0" in the appropriate spot indicating that a
// file is not parsable.
//////////////////////////////////////////////////////////////////////

template <class RealT>
void ComputationEngine<RealT>::CheckParsability(std::vector<RealT> &result, 
                                                const NonSharedInfo &nonshared)
{

    // load training example
    const SStruct &sstruct = descriptions[nonshared.index].sstruct;
    inference_engine.LoadSequence(sstruct);

    // conditional inference
    inference_engine.LoadValues(std::vector<RealT>(parameter_manager.GetNumLogicalParameters()));
    inference_engine.UseConstraints(sstruct.GetMapping());
    inference_engine.UpdateEvidenceStructures();

    inference_engine.ComputeViterbi();
    RealT conditional_score = inference_engine.GetViterbiScore();

    // check for bad parse
    result.clear();
    result.resize(descriptions.size());
    result[nonshared.index] = (conditional_score < RealT(NEG_INF/2) ? 0 : 1);
}

//////////////////////////////////////////////////////////////////////
// ComputationEngine::ComputeSolutionNormBound()
//
// Compute the max entropy and loss possible for an example.
//////////////////////////////////////////////////////////////////////

template<class RealT>
void ComputationEngine<RealT>::ComputeSolutionNormBound(std::vector<RealT> &result, 
                                                        const SharedInfo<RealT> &shared,
                                                        const NonSharedInfo &nonshared)
{

    RealT max_entropy = RealT(0);
    RealT max_loss = RealT(0);

    // load training example
    const SStruct &sstruct = descriptions[nonshared.index].sstruct;
    inference_engine.LoadSequence(sstruct);

    // load parameters
    const std::vector<RealT> w(parameter_manager.GetNumLogicalParameters(), RealT(0));
    inference_engine.LoadValues(w);
    inference_engine.UpdateEvidenceStructures();

    // perform computation
#if !SMOOTH_MAX_MARGIN
    if (!options.GetBoolValue("viterbi_parsing"))
#endif
    {
        inference_engine.ComputeInside();
        max_entropy += inference_engine.ComputeLogPartitionCoefficient();
    }
        
#if defined(HAMMING_LOSS)
    inference_engine.UseLoss(sstruct.GetMapping(), RealT(HAMMING_LOSS));
    inference_engine.ComputeViterbi();
    max_loss += inference_engine.GetViterbiScore();
#endif

    result.clear();
    result.resize(descriptions.size());
    result[nonshared.index] = max_entropy / shared.log_base + max_loss;

    result *= RealT(descriptions[nonshared.index].weight);
}

//////////////////////////////////////////////////////////////////////
// ComputationEngine::ComputeGradientNormBound()
//
// Compute the max L1 norm for the features of an example.
// Return a vector with this value in the appropriate spot for
// this example.
//////////////////////////////////////////////////////////////////////

template<class RealT>
void ComputationEngine<RealT>::ComputeGradientNormBound(std::vector<RealT> &result,
                                                        const NonSharedInfo &nonshared)
{
    // load training example
    const SStruct &sstruct = descriptions[nonshared.index].sstruct;
    inference_engine.LoadSequence(sstruct);

    // load parameters
    const std::vector<RealT> w(parameter_manager.GetNumLogicalParameters(), RealT(1));
    inference_engine.LoadValues(w);
    inference_engine.UpdateEvidenceStructures();

    // perform inference
    inference_engine.ComputeViterbi();
    const RealT max_L1_norm = inference_engine.GetViterbiScore();

    result.clear();
    result.resize(descriptions.size());
    result[nonshared.index] = max_L1_norm;
}

//////////////////////////////////////////////////////////////////////
// ComputationEngine::ComputeLoss()
//
// Return a vector containing a single entry with the loss value.
//////////////////////////////////////////////////////////////////////

template<class RealT>
void ComputationEngine<RealT>::ComputeLoss(std::vector<RealT> &result, 
                                           const SharedInfo<RealT> &shared,
                                           const NonSharedInfo &nonshared)
{
    // load training example
    const SStruct &sstruct = descriptions[nonshared.index].sstruct;
    inference_engine.LoadSequence(sstruct);

    // load parameters
    const std::vector<RealT> w(shared.w, shared.w + parameter_manager.GetNumLogicalParameters());
    inference_engine.LoadValues(w * shared.log_base);
    inference_engine.UpdateEvidenceStructures();

    // perform inference
    SStruct *solution;
    if (options.GetBoolValue("viterbi_parsing"))
    {
        inference_engine.ComputeViterbi();
        solution = new SStruct(sstruct);
        solution->SetMapping(inference_engine.PredictPairingsViterbi());
    }
    else
    {
        inference_engine.ComputeInside();
        inference_engine.ComputeOutside();
        inference_engine.ComputePosterior();
        solution = new SStruct(sstruct);
        solution->SetMapping(inference_engine.PredictPairingsPosterior(shared.gamma));
    }

    // compute loss
    if (!shared.use_loss) Error("Must be using loss function in order to compute loss.");
#if defined(HAMMING_LOSS)
    inference_engine.UseLoss(sstruct.GetMapping(), shared.log_base * RealT(HAMMING_LOSS));
#endif
    inference_engine.LoadValues(std::vector<RealT>(w.size()));
    inference_engine.UseConstraints(solution->GetMapping());
    inference_engine.UpdateEvidenceStructures();
    inference_engine.ComputeViterbi();

    delete solution;

    result.clear();
    result.push_back(inference_engine.GetViterbiScore());

    result *= RealT(descriptions[nonshared.index].weight);
    result.back() /= shared.log_base;
}

//////////////////////////////////////////////////////////////////////
// ComputationEngine::ComputeMStepFunctionAndGradient();
//
// Return a vector containing the gradient and function value for the
// M-step in EM training. Specifically, uses the expected sufficient 
// statistics for the feature counts under the evidence.
//////////////////////////////////////////////////////////////////////

template<class RealT>
void ComputationEngine<RealT>::ComputeMStepFunctionAndGradient(std::vector<RealT> &result, 
                                                          const SharedInfo<RealT> &shared,
                                                          const NonSharedInfo &nonshared,
                                                          bool need_gradient)
{
    // load training example
    const SStruct &sstruct = descriptions[nonshared.index].sstruct;
    inference_engine.LoadSequence(sstruct);

    // load parameters
    const std::vector<RealT> w(shared.w, shared.w + parameter_manager.GetNumLogicalParameters());
    inference_engine.LoadValues(w * shared.log_base);
    inference_engine.UpdateEvidenceStructures();

#if defined(HAMMING_LOSS)
    Error("HAMMING_LOSS not implemented within EM training");
    if (shared.use_loss) inference_engine.UseLoss(sstruct.GetMapping(), shared.log_base * RealT(HAMMING_LOSS));
#endif
    
    // unconditional inference - unchanged in M-step optimization
    RealT unconditional_score;
    std::vector<RealT> unconditional_counts;

    if (shared.use_nonsmooth)
    {
        Error("Viterbi training not supported within EM training");
        // To support this, need to add a new function ComputeViterbiEM which does MAP with evidence potential
        inference_engine.ComputeViterbi();
        unconditional_score = inference_engine.GetViterbiScore();
        if (need_gradient) unconditional_counts = inference_engine.ComputeViterbiFeatureCounts();
    }
    else
    {
        inference_engine.ComputeInside();
        unconditional_score = inference_engine.ComputeLogPartitionCoefficient();
        if (need_gradient)
        {
            inference_engine.ComputeOutside();
            unconditional_counts = inference_engine.ComputeFeatureCountExpectations();
        }
    }

    // conditional inference - need to use ESS (if you don't know the labels)
    RealT conditional_score;
    std::vector<RealT> conditional_counts;
    
    if (shared.use_nonsmooth)
    {
        Error("Viterbi training not supported within EM training");
        inference_engine.ComputeViterbi();
        conditional_score = inference_engine.GetViterbiScore();
        if (need_gradient) conditional_counts = inference_engine.ComputeViterbiFeatureCounts();
    }
    else
    {
	// If we don't know the true structure

	if (!sstruct.HasStruct()) {
            // Compute the ESS - the function value should be dot(w, ESS)
            
	    // No constraints used here if we don't know what the actual structure is.
            inference_engine.ComputeInsideESS();
            inference_engine.ComputeOutsideESS();
            conditional_counts = inference_engine.ComputeFeatureCountExpectationsESS();
            conditional_score = DotProduct(w, conditional_counts);

	} else {

            // Otherwise use the true feature counts

	    // Clamp to true
            inference_engine.UseConstraints(sstruct.GetMapping());
            inference_engine.UpdateEvidenceStructures();

            inference_engine.ComputeInside();
            conditional_score = inference_engine.ComputeLogPartitionCoefficient();
            if (need_gradient)
            {
                inference_engine.ComputeOutside();
                conditional_counts = inference_engine.ComputeFeatureCountExpectations();
            }
	} 
    }

    result.clear();

    // compute subgradient
    if (need_gradient) result = unconditional_counts - conditional_counts;
    
    // compute function value
    Assert(conditional_score <= unconditional_score, "Conditional score cannot exceed unconditional score.");
    result.push_back(unconditional_score - conditional_score);
    
    // check for bad parse
    if (conditional_score < RealT(NEG_INF/2))
    {
        std::cerr << "Unexpected bad parse for file: " << descriptions[nonshared.index].input_filename << std::endl;
        fill(result.begin(), result.end(), RealT(0));
        return;
    }

    if (NONCONVEX_MULTIPLIER != 0)
    {
        Error("Nonconvex training not supported within EM training");
      
#if STOCHASTIC_GRADIENT  // TODO: need to check whether this is correct
        if (shared.use_loss) inference_engine.UseLoss(sstruct.GetMapping(), RealT(0));
        
        // unconditional counts
        inference_engine.UseMapping(std::vector<int>(sstruct.GetLength() + 1, UNKNOWN));  // TODO: call after this: inference_engine.UpdateEvidenceStructures(); 
        if (shared.use_nonsmooth)
        {
            inference_engine.ComputeViterbi();
            unconditional_score = inference_engine.GetViterbiScore();
            if (need_gradient) unconditional_counts = inference_engine.ComputeViterbiFeatureCounts();
        }
        else
        {
            inference_engine.ComputeInside();
            unconditional_score = inference_engine.ComputeLogPartitionCoefficient();
            if (need_gradient)
            {
                inference_engine.ComputeOutside();
                unconditional_counts = inference_engine.ComputeFeatureCountExpectations();
            }
        }
        
        // conditional counts
        inference_engine.UseMapping(sstruct.GetMapping());
        if (shared.use_nonsmooth)
        {
            inference_engine.ComputeViterbi();
            unconditional_score = inference_engine.GetViterbiScore();
            if (need_gradient) unconditional_counts = inference_engine.ComputeViterbiFeatureCounts();
        }
        else
        {
            inference_engine.ComputeInside();
            unconditional_score = inference_engine.ComputeLogPartitionCoefficient();
            if (need_gradient)
            {
                inference_engine.ComputeOutside();
                unconditional_counts = inference_engine.ComputeFeatureCountExpectations();
            }
        }
        
        std::vector<RealT> result2;
        
        // compute subgradient
        if (need_gradient) result2 = unconditional_counts - conditional_counts;
        
        // compute function value
        Assert(conditional_score <= unconditional_score, "Conditional score cannot exceed unconditional score.");
        result2.push_back(unconditional_score - conditional_score);
        
        // check for bad parse
        if (conditional_score < RealT(NEG_INF/2))
        {
            std::cerr << "Unexpected bad parse for file: " << descriptions[nonshared.index].input_filename << std::endl;
            fill(result.begin(), result.end(), 0);
            return;
        }
        
        result -= NONCONVEX_MULTIPLIER * result2;
#endif
    }

    // avoid precision problems
    if (result.back() < 0)
    {
        if (result.back() < -1e-6)
        {
            std::cerr << "Encountered negative function value for " << descriptions[nonshared.index].input_filename << ": " << result.back() << std::endl;
            parameter_manager.WriteToFile(SPrintF("neg_params.%s", GetBaseName(descriptions[nonshared.index].input_filename).c_str()), w);
            exit(0);
        }
        std::fill(result.begin(), result.end(), RealT(0));
        return;
    }

    result *= RealT(descriptions[nonshared.index].weight);
    result.back() /= shared.log_base;
}


//////////////////////////////////////////////////////////////////////
// ComputationEngine::ComputeGammaMLEFunctionAndGradient();
//
// Return a vector containing the gradient and function value for the
// M-step in EM training. Specifically, uses the expected sufficient 
// statistics for the feature counts under the evidence.
//////////////////////////////////////////////////////////////////////

template<class RealT>
void ComputationEngine<RealT>::ComputeGammaMLEFunctionAndGradient(std::vector<RealT> &result, 
                                                          const SharedInfo<RealT> &shared,
                                                          const NonSharedInfo &nonshared,
                                                          bool need_gradient)
{
    // load training example
    const SStruct &sstruct = descriptions[nonshared.index].sstruct;

    int which_data = shared.which_data;

    // ignore structures that have no evidence for this dataset
    if (!sstruct.HasEvidence(which_data))
    {
        result.clear();
        if (need_gradient) {
            result.push_back(RealT(0));  // sum d
            result.push_back(RealT(0));  // sumlog d (for MLE) or sum d^2 (for MM)
            result.push_back(RealT(0));  // num examples
        }
        result.push_back(RealT(0));  // ll
        return;
    }

    inference_engine.LoadSequence(sstruct);
    
    // set the true structure if it exists
    inference_engine.UseConstraints(sstruct.GetMapping());

    // load parameters
    const std::vector<RealT> w(shared.w, shared.w + parameter_manager.GetNumLogicalParameters());
    inference_engine.LoadValues(w * shared.log_base);
#if defined(HAMMING_LOSS)
    Error("HAMMING_LOSS not implemented within EM training");
//    if (shared.use_loss) inference_engine.UseLoss(sstruct.GetMapping(), shared.log_base * RealT(HAMMING_LOSS));
#endif

    inference_engine.UpdateEvidenceStructures();


    RealT update_gammamle_sssum;
    RealT update_gammamle_sssumlog;
    RealT update_gammamle_num_examples;
    RealT update_gammamle_ssq;
    RealT ll = 0;
    std::vector<RealT> stats(3);

    int j = shared.id_base; 
    int k = shared.id_pairing; 
    bool areZeros = (bool)shared.areZeros; 
    RealT scale = (RealT)shared.evidence_data_scale;
    std::vector<int> evidence_cpd_id;
    evidence_cpd_id.push_back(j);
    evidence_cpd_id.push_back(k);
    evidence_cpd_id.push_back(areZeros);
    
    // Note: we adjust the suff stats using the scale parameter as follows:
    // sssum => ssum / scale
    // sssumlog => ssumlog - N * log scale
    // esssumlog => essumlog - (log scale) * ssq

    RealT current_k = exp(w[parameter_manager.GetLogicalIndex(inference_engine.GetLogScoreEvidence(0,j,k,which_data))]);
    RealT current_theta = exp(w[parameter_manager.GetLogicalIndex(inference_engine.GetLogScoreEvidence(1,j,k,which_data))]);

    if (shared.use_nonsmooth)
    {
        Error("Viterbi training not supported within EM training");
        //        inference_engine.ComputeViterbi();
//        conditional_score = inference_engine.GetViterbiScore();
//        if (need_gradient) conditional_counts = inference_engine.ComputeViterbiFeatureCounts();
    }
    else
    {
	if (!sstruct.HasStruct()) {

            // Compute expected suff stats and use these in the update equations
            inference_engine.ComputeInsideESS();
            inference_engine.ComputeOutsideESS();
            inference_engine.ComputePosteriorESS();

  	    // If we don't know the true structure
	    stats = inference_engine.ComputeGammaMLEESS(evidence_cpd_id, !areZeros, !areZeros, which_data);  
            update_gammamle_num_examples = inference_engine.GetNumExamplesSeq(evidence_cpd_id,false, which_data);  

            // adjust suff stats as noted above
            update_gammamle_sssum = stats[0];
            update_gammamle_ssq = stats[2];
            update_gammamle_sssumlog = stats[1];

            if (!areZeros)
                ll = (current_k-1)*update_gammamle_sssumlog - update_gammamle_sssum / current_theta - update_gammamle_num_examples * current_k * log(current_theta) - update_gammamle_num_examples * lgamma(current_k);
            else {
                // in the LL calculation, ignore the 0-counts and use MLE SS
                stats = inference_engine.ComputeGammaMLEESS(evidence_cpd_id, true, true, which_data);  // either: true, true (so don't ignore zeros and use MLE SS) OR false, false (so ignore zeros and use method of
		RealT update_gammamle_sssumlog_nozeros = stats[1];
                RealT update_gammamle_num_examples_nozeros = (int)inference_engine.GetNumExamplesSeq(evidence_cpd_id,true, which_data);  // ignore the zeros
                ll = (current_k-1)*update_gammamle_sssumlog_nozeros - update_gammamle_sssum / current_theta - update_gammamle_num_examples_nozeros * current_k * log(current_theta) - update_gammamle_num_examples_nozeros * lgamma(current_k);
            }

            // adjust suff stats as noted above
            update_gammamle_sssum = update_gammamle_sssum / scale;
            update_gammamle_sssumlog = update_gammamle_sssumlog - update_gammamle_ssq * log(scale);

	} else {

  	    // If we know the true structure
	    stats = inference_engine.ComputeGammaMLESS(evidence_cpd_id, !areZeros, !areZeros, which_data);  
	    update_gammamle_num_examples = inference_engine.GetNumExamplesSeqPairing(evidence_cpd_id,false, which_data);  

            // adjust suff stats as noted above
            update_gammamle_sssum = stats[0];
            update_gammamle_sssumlog = stats[1];

            if (!areZeros)
                ll = (current_k-1)*update_gammamle_sssumlog - update_gammamle_sssum / current_theta - update_gammamle_num_examples * current_k * log(current_theta) - update_gammamle_num_examples * lgamma(current_k);
            else {
                // in the LL calculation, ignore the 0-counts and use MLE SS
                stats = inference_engine.ComputeGammaMLESS(evidence_cpd_id, true, true, which_data);  
		RealT update_gammamle_sssumlog_nozeros = stats[1];
                RealT update_gammamle_num_examples_nozeros = (int)inference_engine.GetNumExamplesSeqPairing(evidence_cpd_id,true, which_data);  // ignore the zeros
                ll = (current_k-1)*update_gammamle_sssumlog_nozeros - update_gammamle_sssum / current_theta - update_gammamle_num_examples_nozeros * current_k * log(current_theta) - update_gammamle_num_examples_nozeros * lgamma(current_k);
            }

            // adjust suff stats as noted above
            update_gammamle_sssum = update_gammamle_sssum / scale;
            update_gammamle_sssumlog = update_gammamle_sssumlog - update_gammamle_num_examples * log(scale);

	}
    }

    result.clear();

    if (need_gradient) {
         result.push_back(update_gammamle_sssum);
         result.push_back(update_gammamle_sssumlog);
         result.push_back(update_gammamle_num_examples);
    }

    // compute function value
    result.push_back(ll);
    
/*
    // check for bad parse
    if (conditional_score < RealT(NEG_INF/2))
    {
        std::cerr << "Unexpected bad parse for file: " << descriptions[nonshared.index].input_filename << std::endl;
        fill(result.begin(), result.end(), RealT(0));
        return;
    }

    if (NONCONVEX_MULTIPLIER != 0)
    {
        Error("Nonconvex training not supported within EM training");
#if STOCHASTIC_GRADIENT
#endif
    }

    // avoid precision problems
    if (result.back() < 0)
    {
        if (result.back() < -1e-6)
        {
            std::cerr << "Encountered negative function value for " << descriptions[nonshared.index].input_filename << ": " << result.back() << std::endl;
            parameter_manager.WriteToFile(SPrintF("neg_params.%s", GetBaseName(descriptions[nonshared.index].input_filename).c_str()), w);
            exit(0);
        }
        std::fill(result.begin(), result.end(), RealT(0));
        return;
    }
*/

    result *= RealT(descriptions[nonshared.index].weight);

}


//////////////////////////////////////////////////////////////////////
// ComputationEngine::ComputeGammaMLEScalingFactor();
//
// Return a vector containing the gradient and function value for the
// M-step in EM training. Specifically, uses the expected sufficient 
// statistics for the feature counts under the evidence.
//////////////////////////////////////////////////////////////////////

template<class RealT>
void ComputationEngine<RealT>::ComputeGammaMLEScalingFactor(std::vector<RealT> &result, 
                                                          const SharedInfo<RealT> &shared,
                                                          const NonSharedInfo &nonshared)
{

    // load training example
    const SStruct &sstruct = descriptions[nonshared.index].sstruct;
    int which_data = shared.which_data;

    // ignore structures that have no evidence for this dataset
    if (!sstruct.HasEvidence(which_data))
    {
        result.clear();
        result.push_back(0);  // sum
        result.push_back(0);  // num_examples
        return;
    }

    inference_engine.LoadSequence(sstruct);
    
    // set the true structure if it exists
    inference_engine.UseConstraints(sstruct.GetMapping());

    // load parameters
    const std::vector<RealT> w(shared.w, shared.w + parameter_manager.GetNumLogicalParameters());
    inference_engine.LoadValues(w * shared.log_base);

    inference_engine.UpdateEvidenceStructures();

    RealT update_gammamle_sssum;
    RealT update_gammamle_num_examples;

    int j = shared.id_base;
    int k = shared.id_pairing;
    std::vector<int> evidence_cpd_id;
    evidence_cpd_id.push_back(j);
    evidence_cpd_id.push_back(k);

    if (!sstruct.HasStruct()) {

        // If we don't know the true structure
        inference_engine.ComputeInsideESS();
        inference_engine.ComputeOutsideESS();
        inference_engine.ComputePosteriorESS();

	update_gammamle_sssum = inference_engine.ComputeGammaMLESum(evidence_cpd_id, true, true, which_data);  // ignore pairing since don't know it, use posterior
        update_gammamle_num_examples = inference_engine.GetNumExamplesSeq(evidence_cpd_id,false, which_data);  // don't ignore zeros
    } else {

        // If we don't know the true structure
	update_gammamle_sssum = inference_engine.ComputeGammaMLESum(evidence_cpd_id, false, false, which_data);  // don't ignore pairing since we know it, don't use posterior
        update_gammamle_num_examples = inference_engine.GetNumExamplesSeqPairing(evidence_cpd_id,false, which_data);  // don't ignore zeros
    }

    result.clear();
    result.push_back(update_gammamle_sssum);
    result.push_back(update_gammamle_num_examples);
}


template<class RealT>
void ComputationEngine<RealT>::CheckZerosInData(std::vector<RealT> &result, 
                                                          const SharedInfo<RealT> &shared,
                                                          const NonSharedInfo &nonshared)
{

    int which_data = shared.which_data;

    // load training example
    const SStruct &sstruct = descriptions[nonshared.index].sstruct;
    inference_engine.LoadSequence(sstruct);

    // conditional inference
    inference_engine.LoadValues(std::vector<RealT>(parameter_manager.GetNumLogicalParameters()));
    inference_engine.UseConstraints(sstruct.GetMapping());

    result.clear();
    result.resize(descriptions.size());
    // ignore structures that only have ground truth

    // ignore structures that have no evidence for this dataset
    if (!sstruct.HasEvidence(which_data))
    {
        result[nonshared.index] = 0;
        return;
    }

    inference_engine.UpdateEvidenceStructures(which_data);

    int areZeros;
    if (!sstruct.HasStruct()) {
        areZeros = inference_engine.AreZerosInSeq(shared.id_base, shared.which_data);
    } else {
        areZeros = inference_engine.AreZerosInSeqPairing(shared.id_base, shared.id_pairing, shared.which_data);
    }

    result[nonshared.index] = (RealT)areZeros;
}


//////////////////////////////////////////////////////////////////////
// ComputationEngine::ComputeFunctionAndGradient();
//
// Return a vector containing the gradient and function value.
//////////////////////////////////////////////////////////////////////

template<class RealT>
void ComputationEngine<RealT>::ComputeFunctionAndGradient(std::vector<RealT> &result, 
                                                          const SharedInfo<RealT> &shared,
                                                          const NonSharedInfo &nonshared,
                                                          bool need_gradient)
{

    // load training example
    const SStruct &sstruct = descriptions[nonshared.index].sstruct;
    inference_engine.LoadSequence(sstruct);

    // load parameters
    const std::vector<RealT> w(shared.w, shared.w + parameter_manager.GetNumLogicalParameters());
    inference_engine.LoadValues(w * shared.log_base);
#if defined(HAMMING_LOSS)
    if (shared.use_loss) inference_engine.UseLoss(sstruct.GetMapping(), shared.log_base * RealT(HAMMING_LOSS));
#endif
    
    // unconditional inference
    RealT unconditional_score;
    std::vector<RealT> unconditional_counts;

    if (shared.use_nonsmooth)
    {
        inference_engine.ComputeViterbi();
        unconditional_score = inference_engine.GetViterbiScore();
        if (need_gradient) unconditional_counts = inference_engine.ComputeViterbiFeatureCounts();
    }
    else
    {
        inference_engine.ComputeInside();
        unconditional_score = inference_engine.ComputeLogPartitionCoefficient();
        if (need_gradient)
        {
            inference_engine.ComputeOutside();
            unconditional_counts = inference_engine.ComputeFeatureCountExpectations();
        }
    }

    // conditional inference
    RealT conditional_score;
    std::vector<RealT> conditional_counts;

    inference_engine.UseConstraints(sstruct.GetMapping());
    if (shared.use_nonsmooth)
    {
        inference_engine.ComputeViterbi();
        conditional_score = inference_engine.GetViterbiScore();
        if (need_gradient) conditional_counts = inference_engine.ComputeViterbiFeatureCounts();
    }
    else
    {
        inference_engine.ComputeInside();
        conditional_score = inference_engine.ComputeLogPartitionCoefficient();
        if (need_gradient)
        {
            inference_engine.ComputeOutside();
            conditional_counts = inference_engine.ComputeFeatureCountExpectations();
        }
    }

    result.clear();

    // compute subgradient
    if (need_gradient) result = unconditional_counts - conditional_counts;
    
    // compute function value
    Assert(conditional_score <= unconditional_score, "Conditional score cannot exceed unconditional score.");
    result.push_back(unconditional_score - conditional_score);
    
    // check for bad parse
    if (conditional_score < RealT(NEG_INF/2))
    {
        std::cerr << "Unexpected bad parse for file: " << descriptions[nonshared.index].input_filename << std::endl;
        fill(result.begin(), result.end(), RealT(0));
        return;
    }

    if (NONCONVEX_MULTIPLIER != 0)
    {
        
#if STOCHASTIC_GRADIENT
        if (shared.use_loss) inference_engine.UseLoss(sstruct.GetMapping(), RealT(0));
        
        // unconditional counts
        inference_engine.UseMapping(std::vector<int>(sstruct.GetLength() + 1, UNKNOWN));
        if (shared.use_nonsmooth)
        {
            inference_engine.ComputeViterbi();
            unconditional_score = inference_engine.GetViterbiScore();
            if (need_gradient) unconditional_counts = inference_engine.ComputeViterbiFeatureCounts();
        }
        else
        {
            inference_engine.ComputeInside();
            unconditional_score = inference_engine.ComputeLogPartitionCoefficient();
            if (need_gradient)
            {
                inference_engine.ComputeOutside();
                unconditional_counts = inference_engine.ComputeFeatureCountExpectations();
            }
        }
        
        // conditional counts
        inference_engine.UseMapping(sstruct.GetMapping());
        if (shared.use_nonsmooth)
        {
            inference_engine.ComputeViterbi();
            unconditional_score = inference_engine.GetViterbiScore();
            if (need_gradient) unconditional_counts = inference_engine.ComputeViterbiFeatureCounts();
        }
        else
        {
            inference_engine.ComputeInside();
            unconditional_score = inference_engine.ComputeLogPartitionCoefficient();
            if (need_gradient)
            {
                inference_engine.ComputeOutside();
                unconditional_counts = inference_engine.ComputeFeatureCountExpectations();
            }
        }
        
        std::vector<RealT> result2;
        
        // compute subgradient
        if (need_gradient) result2 = unconditional_counts - conditional_counts;
        
        // compute function value
        Assert(conditional_score <= unconditional_score, "Conditional score cannot exceed unconditional score.");
        result2.push_back(unconditional_score - conditional_score);
        
        // check for bad parse
        if (conditional_score < RealT(NEG_INF/2))
        {
            std::cerr << "Unexpected bad parse for file: " << descriptions[nonshared.index].input_filename << std::endl;
            fill(result.begin(), result.end(), 0);
            return;
        }
        
        result -= NONCONVEX_MULTIPLIER * result2;
#endif
    }

    // avoid precision problems
    if (result.back() < 0)
    {
        if (result.back() < -1e-6)
        {
            std::cerr << "Encountered negative function value for " << descriptions[nonshared.index].input_filename << ": " << result.back() << std::endl;
            parameter_manager.WriteToFile(SPrintF("neg_params.%s", GetBaseName(descriptions[nonshared.index].input_filename).c_str()), w);
            exit(0);
        }
        std::fill(result.begin(), result.end(), RealT(0));
        return;
    }

    result *= RealT(descriptions[nonshared.index].weight);
    result.back() /= shared.log_base;
}


//////////////////////////////////////////////////////////////////////
// ComputationEngine::ComputeFunctionAndGradientSE();
//
// Return a vector containing the gradient and function value for the
// M-step in EM training. Specifically, uses the expected sufficient 
// statistics for the feature counts under the evidence.
//////////////////////////////////////////////////////////////////////

template<class RealT>
void ComputationEngine<RealT>::ComputeFunctionAndGradientSE(std::vector<RealT> &result, 
                                                            const SharedInfo<RealT> &shared,
                                                            const NonSharedInfo &nonshared,
                                                            bool need_gradient)
{

    // load training example
    const SStruct &sstruct = descriptions[nonshared.index].sstruct;
    inference_engine.LoadSequence(sstruct);

    // load parameters
    const std::vector<RealT> w(shared.w, shared.w + parameter_manager.GetNumLogicalParameters());
    inference_engine.LoadValues(w * shared.log_base);
    inference_engine.UpdateEvidenceStructures();

#if defined(HAMMING_LOSS)
    Error("HAMMING_LOSS not implemented within EM training");
    if (shared.use_loss) inference_engine.UseLoss(sstruct.GetMapping(), shared.log_base * RealT(HAMMING_LOSS));
#endif
    
    // unconditional inference - only required if we want the gradient for evidence-only data
    RealT unconditional_score = 0;
    std::vector<RealT> unconditional_counts;

    if (shared.use_nonsmooth)
    {
        Error("Viterbi training not supported within EM training");
        // To support this, need to add a new function ComputeViterbiEM which does MAP with evidence potential
        inference_engine.ComputeViterbi();
        unconditional_score = inference_engine.GetViterbiScore();
        if (need_gradient) unconditional_counts = inference_engine.ComputeViterbiFeatureCounts();
    }
    else
    {
        inference_engine.ComputeInside();
        // We still need to compute Z for the Contrafold distribution even when
        // we have evidence because we need it to normalize the Contrafold
        // potentials to get log P(d | x) as the log-partition function of Q(y).
        unconditional_score = inference_engine.ComputeLogPartitionCoefficient();
        if (need_gradient)
        {
            inference_engine.ComputeOutside();
            unconditional_counts = inference_engine.ComputeFeatureCountExpectations();
        }
    }

    // conditional inference - need to use ESS (if you don't know the labels)
    RealT conditional_score;
    std::vector<RealT> conditional_counts;

    if (shared.use_nonsmooth)
    {
        Error("Viterbi training not supported within EM training");
        inference_engine.ComputeViterbi();
        conditional_score = inference_engine.GetViterbiScore();
        if (need_gradient) conditional_counts = inference_engine.ComputeViterbiFeatureCounts();
    }
    else
    {
        // If we don't know the true structure
        if (!sstruct.HasStruct()) {
            // Compute the ESS - the function value is log Q = log sum_y P(y,d|x)
            // No constraints used here if we don't know what the actual structure is.
            inference_engine.ComputeInsideESS();
            conditional_score = inference_engine.ComputeLogPartitionCoefficientESS();

            if (need_gradient) {
                inference_engine.ComputeOutsideESS();
                inference_engine.ComputePosteriorESS(); // needed for Gamma ESS
                conditional_counts = inference_engine.ComputeFeatureCountExpectationsESS();
            }
        } else {
            // Otherwise use the true feature counts
            // Clamp to true
            inference_engine.UseConstraints(sstruct.GetMapping());
            inference_engine.UpdateEvidenceStructures();

            inference_engine.ComputeInside();
            conditional_score = inference_engine.ComputeLogPartitionCoefficient();
            if (need_gradient)
            {
                inference_engine.ComputeOutside();
                conditional_counts = inference_engine.ComputeFeatureCountExpectations();
            }
        }
    }

    result.clear();

    // compute gradient
    if (need_gradient) result = unconditional_counts - conditional_counts;

    RealT function_value = 0;

    // Compute Gamma CPD (Expected) Sufficient Stats
    int num_data_sources = options.GetIntValue("num_data_sources");

    for (int dataset_id = 0; dataset_id < num_data_sources; dataset_id++) {
        for (int i = 0; i < M; i++) { // nucleotide
            for (int j = 0; j < 2; j++) { // base pairing
                RealT sum_d = 0;
                RealT sum_log_d = 0;
                RealT N = 0;

                std::vector<int> evidence_cpd_id;
                evidence_cpd_id.push_back(i);
                evidence_cpd_id.push_back(j);

                if (sstruct.HasEvidence(dataset_id)) {
                    std::vector<RealT> stats(3, 0);

                    if (sstruct.HasStruct()) {
                        stats = inference_engine.ComputeGammaMLESS(evidence_cpd_id, true, true, dataset_id);
                    } else if (need_gradient) {
                        stats = inference_engine.ComputeGammaMLEESS(evidence_cpd_id, true, true, dataset_id);
                    }
                    sum_d = stats[0];
                    sum_log_d = stats[1];
                    N = stats[2];
                }

                int index_k = parameter_manager.GetLogicalIndex(
                    inference_engine.GetLogScoreEvidence(0, i, j, dataset_id));
                int index_theta = parameter_manager.GetLogicalIndex(
                    inference_engine.GetLogScoreEvidence(1, i, j, dataset_id));

                RealT k = exp(w[index_k]);
                RealT log_theta = w[index_theta];
                RealT theta_inv = exp(-log_theta);

                // Add to the function_value (only if we have struct + evidence)
                if (sstruct.HasStruct()) {
                    function_value -= (k-1)*sum_log_d - sum_d*theta_inv - N*k*log_theta - N*lgamma(k);
                }

                // Compute gradient (use implicit differentiation for actual parameters log(k) and log(theta))
                if (need_gradient) {
                    RealT grad_k = -(sum_log_d - N*log_theta - N*Psi(k)) * k;
                    RealT grad_theta = -(sum_d*theta_inv - N*k);

                    result[index_k] = grad_k;
                    result[index_theta] = grad_theta;
                }

            }
        }

    }

   // compute function value
    RealT function_value_nonevidence = unconditional_score - conditional_score;
    function_value += function_value_nonevidence;

    if (sstruct.HasStruct()) {
        Assert(conditional_score <= unconditional_score, "Conditional score cannot exceed unconditional score.");
    }

    result.push_back(function_value);

    // check for bad parse
    if (conditional_score < RealT(NEG_INF/2))
    {
        std::cerr << "Unexpected bad parse for file: " << descriptions[nonshared.index].input_filename << std::endl;
        fill(result.begin(), result.end(), RealT(0));
        return;
    }

    if (NONCONVEX_MULTIPLIER != 0)
    {
        Error("Nonconvex training not supported within EM training");
      
#if STOCHASTIC_GRADIENT  // TODO: need to check whether this is correct
        if (shared.use_loss) inference_engine.UseLoss(sstruct.GetMapping(), RealT(0));
        
        // unconditional counts
        inference_engine.UseMapping(std::vector<int>(sstruct.GetLength() + 1, UNKNOWN));  
        if (shared.use_nonsmooth)
        {
            inference_engine.ComputeViterbi();
            unconditional_score = inference_engine.GetViterbiScore();
            if (need_gradient) unconditional_counts = inference_engine.ComputeViterbiFeatureCounts();
        }
        else
        {
            inference_engine.ComputeInside();
            unconditional_score = inference_engine.ComputeLogPartitionCoefficient();
            if (need_gradient)
            {
                inference_engine.ComputeOutside();
                unconditional_counts = inference_engine.ComputeFeatureCountExpectations();
            }
        }
        
        // conditional counts
        inference_engine.UseMapping(sstruct.GetMapping());
        if (shared.use_nonsmooth)
        {
            inference_engine.ComputeViterbi();
            unconditional_score = inference_engine.GetViterbiScore();
            if (need_gradient) unconditional_counts = inference_engine.ComputeViterbiFeatureCounts();
        }
        else
        {
            inference_engine.ComputeInside();
            unconditional_score = inference_engine.ComputeLogPartitionCoefficient();
            if (need_gradient)
            {
                inference_engine.ComputeOutside();
                unconditional_counts = inference_engine.ComputeFeatureCountExpectations();
            }
        }
        
        std::vector<RealT> result2;
        
        // compute subgradient
        if (need_gradient) result2 = unconditional_counts - conditional_counts;
        
        // compute function value
        Assert(conditional_score <= unconditional_score, "Conditional score cannot exceed unconditional score.");
        result2.push_back(unconditional_score - conditional_score);
        
        // check for bad parse
        if (conditional_score < RealT(NEG_INF/2))
        {
            std::cerr << "Unexpected bad parse for file: " << descriptions[nonshared.index].input_filename << std::endl;
            fill(result.begin(), result.end(), 0);
            return;
        }
        
        result -= NONCONVEX_MULTIPLIER * result2;
#endif
    }

    // avoid precision problems
    if (function_value_nonevidence < 0)
    {
        if (function_value_nonevidence < -1e-6 && sstruct.HasStruct())
        {
            std::cerr << "Encountered negative function value for " << descriptions[nonshared.index].input_filename << ": " << result.back() << std::endl;
            parameter_manager.WriteToFile(SPrintF("neg_params.%s", GetBaseName(descriptions[nonshared.index].input_filename).c_str()), w);
            exit(0);
        }
        std::fill(result.begin(), result.end(), RealT(0));
        return;
    }

    result *= RealT(descriptions[nonshared.index].weight);
    result.back() /= shared.log_base;

    // if this is a data-only example, multiply by the hyperparameter for data
    if (!sstruct.HasStruct()) {
    	result *= RealT(shared.hyperparam_data);
    }
}


//////////////////////////////////////////////////////////////////////
// ComputationEngine::ComputeHessianVectorProduct()
//
// Return a vector containing Hv.
//////////////////////////////////////////////////////////////////////

template<class RealT>
void ComputationEngine<RealT>::ComputeHessianVectorProduct(std::vector<RealT> &result, 
                                                           const SharedInfo<RealT> &shared,
                                                           const NonSharedInfo &nonshared)
{
    const std::vector<RealT> w(shared.w, shared.w + parameter_manager.GetNumLogicalParameters());
    const std::vector<RealT> v(shared.v, shared.v + parameter_manager.GetNumLogicalParameters());

    if (options.GetBoolValue("viterbi_parsing"))
    {
        Error("Should not use Hessian-vector products with Viterbi parsing.");
    }
    
    const RealT EPSILON = RealT(1e-8);
    SharedInfo<RealT> shared_temp(shared);
    std::vector<RealT> result2;

    for (size_t i = 0; i < parameter_manager.GetNumLogicalParameters(); i++)
        shared_temp.w[i] = shared.w[i] + EPSILON * v[i];
    ComputeFunctionAndGradient(result, shared_temp, nonshared, true);
    
    for (size_t i = 0; i < parameter_manager.GetNumLogicalParameters(); i++)
        shared_temp.w[i] = shared.w[i] - EPSILON * v[i];
    ComputeFunctionAndGradient(result2, shared_temp, nonshared, true);
    
    result = (result - result2) / (RealT(2) * EPSILON);
}

//////////////////////////////////////////////////////////////////////
// ComputationEngine::Predict()
//
// Predict structure of a single sequence.
//////////////////////////////////////////////////////////////////////

template<class RealT>
void ComputationEngine<RealT>::Predict(std::vector<RealT> &result, 
                                       const SharedInfo<RealT> &shared,
                                       const NonSharedInfo &nonshared)
{
    result.clear();
    
    // load sequence, with constraints if necessary
    const SStruct &sstruct = descriptions[nonshared.index].sstruct;
    inference_engine.LoadSequence(sstruct);
    if (options.GetBoolValue("use_constraints")) inference_engine.UseConstraints(sstruct.GetMapping());

    // load parameters
    const std::vector<RealT> w(shared.w, shared.w + parameter_manager.GetNumLogicalParameters());
    inference_engine.LoadValues(w * shared.log_base);

    inference_engine.UpdateEvidenceStructures();

    // perform inference
    SStruct *solution;
    if (options.GetBoolValue("viterbi_parsing"))
    {
        if (options.GetBoolValue("use_evidence"))
            Error("Viterbi parsing is not supported with evidence yet");
        // Basically, add a ComputeViterbiESS and then call it to support this.

        inference_engine.ComputeViterbi();
        if (options.GetBoolValue("partition_function_only"))
        {
            std::cout << "Viterbi score for \"" << descriptions[nonshared.index].input_filename << "\": " 
                      << inference_engine.GetViterbiScore() << std::endl;
            return;
        }
        solution = new SStruct(sstruct);
        solution->SetMapping(inference_engine.PredictPairingsViterbi());
    }
    else
    {
        if (options.GetBoolValue("use_evidence")) 
        {

            inference_engine.ComputeInsideESS();
            if (options.GetBoolValue("partition_function_only"))
            {
                std::cout << "Log partition coefficient for \"" << descriptions[nonshared.index].input_filename << "\": " 
                          << inference_engine.ComputeLogPartitionCoefficientESS() << std::endl;
                return;
            }
            inference_engine.ComputeOutsideESS();
            inference_engine.ComputePosteriorESS();

        } 
        else
        {

            inference_engine.ComputeInside();
            if (options.GetBoolValue("partition_function_only"))
            {
                std::cout << "Log partition coefficient for \"" << descriptions[nonshared.index].input_filename << "\": " 
                          << inference_engine.ComputeLogPartitionCoefficient() << std::endl;
                return;
            }
            inference_engine.ComputeOutside();
            inference_engine.ComputePosterior();

        }

        solution = new SStruct(sstruct);
        if (options.GetBoolValue("centroid_estimator")) {
            std::cout << "Predicting using centroid estimator." << std::endl;
            solution->SetMapping(inference_engine.PredictPairingsPosteriorCentroid(shared.gamma));
        } else {
            std::cout << "Predicting using MEA estimator." << std::endl;
            solution->SetMapping(inference_engine.PredictPairingsPosterior(shared.gamma));
        }
    }

    // write output
    if (options.GetStringValue("output_parens_destination") != "")
    {
        const std::string filename = MakeOutputFilename(descriptions[nonshared.index].input_filename,
                                                        options.GetStringValue("output_parens_destination"),
                                                        options.GetRealValue("gamma") < 0,
                                                        shared.gamma);
        std::ofstream outfile(filename.c_str());
        if (outfile.fail()) Error("Unable to open output parens file '%s' for writing.", filename.c_str());
        solution->WriteParens(outfile);
        outfile.close();
    }
  
    if (options.GetStringValue("output_bpseq_destination") != "")
    {
        const std::string filename = MakeOutputFilename(descriptions[nonshared.index].input_filename,
                                                        options.GetStringValue("output_bpseq_destination"),
                                                        options.GetRealValue("gamma") < 0,
                                                        shared.gamma);
        std::ofstream outfile(filename.c_str());
        if (outfile.fail()) Error("Unable to open output bpseq file '%s' for writing.", filename.c_str());
        solution->WriteBPSEQ(outfile);
        outfile.close();
    }
    
    if (options.GetStringValue("output_posteriors_destination") != "")
    {
        const std::string filename = MakeOutputFilename(descriptions[nonshared.index].input_filename,
                                                        options.GetStringValue("output_posteriors_destination"),
                                                        options.GetRealValue("gamma") < 0,
                                                        shared.gamma);
        RealT *posterior = inference_engine.GetPosterior(options.GetRealValue("output_posteriors_cutoff"));
        SparseMatrix<RealT> sparse(posterior, sstruct.GetLength()+1, RealT(0));
        delete [] posterior;
        std::ofstream outfile(filename.c_str());
        if (outfile.fail()) Error("Unable to open output posteriors file '%s' for writing.", filename.c_str());
        sparse.PrintSparseBPSEQ(outfile, sstruct.GetSequences()[0]);
        outfile.close();
    }
    
    if (options.GetStringValue("output_parens_destination") == "" &&
        options.GetStringValue("output_bpseq_destination") == "" &&
        options.GetStringValue("output_posteriors_destination") == "")
    {
        WriteProgressMessage("");
        solution->WriteParens(std::cout);
    }
    
    delete solution;
}

//////////////////////////////////////////////////////////////////////
// ComputationEngine::MakeOutputFilename()
//
// Decide on output filename, if any.  The arguments to this function
// consist of (1) a boolean variable indicating whether the output
// destination should be treated as the name of an output directory
// (and the output filename is chosen to match the input file) or
// whether the output destination should be interpreted as the output
// filename; (2) the name of the input file to be processed; and (3)
// the supplied output destination.
//////////////////////////////////////////////////////////////////////

template<class RealT>
std::string ComputationEngine<RealT>::MakeOutputFilename(const std::string &input_filename,
                                                         const std::string &output_destination,
                                                         const bool cross_validation,
                                                         const RealT gamma) const 
{
    if (output_destination == "") return "";

    const std::string dir_name = GetDirName(output_destination);
    const std::string base_name = GetBaseName(output_destination);

    const std::string prefix = (dir_name != "" ? (dir_name + DIR_SEPARATOR_CHAR) : std::string(""));
    
    // check if output directory required
    if (descriptions.size() > 1)
    {
        if (cross_validation)
        {
            return SPrintF("%s%s%c%s.gamma=%lf%c%s",
                           prefix.c_str(),
                           base_name.c_str(),
                           DIR_SEPARATOR_CHAR,
                           base_name.c_str(),
                           double(gamma),
                           DIR_SEPARATOR_CHAR,
                           GetBaseName(input_filename).c_str());
        }
        return SPrintF("%s%s%c%s",
                       prefix.c_str(),
                       base_name.c_str(),
                       DIR_SEPARATOR_CHAR,
                       GetBaseName(input_filename).c_str());
    }
    
    if (cross_validation)
    {
        return SPrintF("%s%s%c%s.gamma=%lf",
                       prefix.c_str(),
                       base_name.c_str(),
                       DIR_SEPARATOR_CHAR,
                       base_name.c_str(),
                       double(gamma));
    }
    return SPrintF("%s%s",
                   prefix.c_str(),
                   base_name.c_str());
}
