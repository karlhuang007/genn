#include "synapseGroup.h"

// Standard includes
#include <algorithm>
#include <cmath>

// GeNN includes
#include "codeGenUtils.h"
#include "standardSubstitutions.h"
#include "utils.h"

// ------------------------------------------------------------------------
// SynapseGroup
// ------------------------------------------------------------------------
void SynapseGroup::setWUVarZeroCopyEnabled(const std::string &var, bool enabled)
{
    // If named variable doesn't exist give error
    VarNameIterCtx wuVars(getWUModel()->getVars());
    if(find(wuVars.nameBegin, wuVars.nameEnd, var) == wuVars.nameEnd) {
        gennError("Cannot find variable " + var);
    }
    // Otherwise add name of variable to set
    else {
        // If enabled, add variable to set
        if(enabled) {
            m_WUVarZeroCopyEnabled.insert(var);
        }
        // Otherwise, remove it
        else {
            m_WUVarZeroCopyEnabled.erase(var);
        }
    }
}

void SynapseGroup::setPSVarZeroCopyEnabled(const std::string &var, bool enabled)
{
    // If named variable doesn't exist give error
    VarNameIterCtx psVars(getPSModel()->getVars());
    if(find(psVars.nameBegin, psVars.nameEnd, var) == psVars.nameEnd) {
        gennError("Cannot find variable " + var);
    }
    // Otherwise
    else  {
        // If enabled, add variable to set
        if(enabled) {
            m_PSVarZeroCopyEnabled.insert(var);
        }
        // Otherwise, remove it
        else {
            m_PSVarZeroCopyEnabled.erase(var);
        }
    }
}

void SynapseGroup::setMaxConnections(unsigned int maxConnections)
{
     if (getMatrixType() & SynapseMatrixConnectivity::SPARSE) {
        m_MaxConnections = maxConnections;
    }
    else {
        gennError("setMaxConn: Synapse group is densely connected. Maxconn variable is not needed in this case.");
    }
}

void SynapseGroup::setSpanType(SpanType spanType)
{
    if (getMatrixType() & SynapseMatrixConnectivity::SPARSE) {
        m_SpanType = spanType;
    }
    else {
        gennError("setSpanType: This function is not enabled for dense connectivity type.");
    }
}

void SynapseGroup::initDerivedParams(double dt)
{
    auto wuDerivedParams = getWUModel()->getDerivedParams();
    auto psDerivedParams = getPSModel()->getDerivedParams();

    // Reserve vector to hold derived parameters
    m_WUDerivedParams.reserve(wuDerivedParams.size());
    m_PSDerivedParams.reserve(psDerivedParams.size());

    // Loop through derived parameters
    for(const auto &d : wuDerivedParams) {
        m_WUDerivedParams.push_back(d.second(m_WUParams, dt));
    }

    // Loop through derived parameters
    for(const auto &d : psDerivedParams) {
        m_PSDerivedParams.push_back(d.second(m_PSParams, dt));
    }
}

void SynapseGroup::calcKernelSizes(unsigned int blockSize, unsigned int &paddedCumSum)
{
    m_PaddedKernelCumSum.first = paddedCumSum;

    if (getMatrixType() & SynapseMatrixConnectivity::SPARSE) {
        if (getSpanType() == SpanType::PRESYNAPTIC) {
            // paddedSize is the lowest multiple of blockSize >= neuronN[synapseSource[i]
            paddedCumSum += ceil((double) getSrcNeuronGroup()->getNumNeurons() / (double) blockSize) * (double) blockSize;
        }
        else {
            // paddedSize is the lowest multiple of blockSize >= maxConn[i]
            paddedCumSum += ceil((double) getMaxConnections() / (double) blockSize) * (double) blockSize;
        }
    }
    else {
        // paddedSize is the lowest multiple of blockSize >= neuronN[synapseTarget[i]]
        paddedCumSum += ceil((double) getTrgNeuronGroup()->getNumNeurons() / (double) blockSize) * (double) blockSize;
    }

    // Store padded cumulative sum
    m_PaddedKernelCumSum.second = paddedCumSum;
}

unsigned int SynapseGroup::getPaddedDynKernelSize(unsigned int blockSize) const
{
    if (getMatrixType() & SynapseMatrixConnectivity::SPARSE) {
        // paddedSize is the lowest multiple of synDynBlkSz >= neuronN[synapseSource[i]] * maxConn[i]
        return ceil((double) getSrcNeuronGroup()->getNumNeurons() * getMaxConnections() / (double) blockSize) * (double) blockSize;
    }
    else {
        // paddedSize is the lowest multiple of synDynBlkSz >= neuronN[synapseSource[i]] * neuronN[synapseTarget[i]]
        return ceil((double) getSrcNeuronGroup()->getNumNeurons() * getTrgNeuronGroup()->getNumNeurons() / (double) blockSize) * (double) blockSize;
    }
}

unsigned int SynapseGroup::getPaddedPostLearnKernelSize(unsigned int blockSize) const
{
    return ceil((double) getSrcNeuronGroup()->getNumNeurons() / (double) blockSize) * (double) blockSize;
}

bool SynapseGroup::isZeroCopyEnabled() const
{
    // If there are any variables return true
    if(!m_WUVarZeroCopyEnabled.empty() || !m_PSVarZeroCopyEnabled.empty())
    {
        return true;
    }

    return false;
}

bool SynapseGroup::isWUVarZeroCopyEnabled(const std::string &var) const
{
    return (m_WUVarZeroCopyEnabled.find(var) != std::end(m_WUVarZeroCopyEnabled));
}

bool SynapseGroup::isPSVarZeroCopyEnabled(const std::string &var) const
{
    return (m_PSVarZeroCopyEnabled.find(var) != std::end(m_PSVarZeroCopyEnabled));
}

bool SynapseGroup::isPSAtomicAddRequired(unsigned int blockSize) const
{
    if (getMatrixType() & SynapseMatrixConnectivity::SPARSE) {
        if (getSpanType() == SpanType::POSTSYNAPTIC && getTrgNeuronGroup()->getNumNeurons() > blockSize) {
            return true;
        }
        if (getSpanType()  == SpanType::PRESYNAPTIC && getSrcNeuronGroup()->getNumNeurons() > blockSize) {
            return true;
        }
    }
    return false;
}

void SynapseGroup::addExtraGlobalParams(const std::string &groupName, std::map<std::string, std::string> &kernelParameters) const
{
    // Synapse kernel
    // --------------
    // Add any of the pre or postsynaptic neuron group's extra global
    // parameters referenced in the sim code to the map of kernel parameters
    addExtraGlobalSimParams(getSrcNeuronGroup()->getName(), "_pre", getSrcNeuronGroup()->getNeuronModel()->getExtraGlobalParams(),
                             kernelParameters);
    addExtraGlobalSimParams(getSrcNeuronGroup()->getName(), "_post", getTrgNeuronGroup()->getNeuronModel()->getExtraGlobalParams(),
                             kernelParameters);

    // Finally add any weight update model extra global
    // parameters referenced in the sim to the map of kernel paramters
    addExtraGlobalSimParams(groupName, "", getWUModel()->getExtraGlobalParams(), kernelParameters);

    // Learn post
    // -----------
    // Add any of the pre or postsynaptic neuron group's extra global
    // parameters referenced in the sim code to the map of kernel parameters
    addExtraGlobalPostLearnParams(getSrcNeuronGroup()->getName(), "_pre", getSrcNeuronGroup()->getNeuronModel()->getExtraGlobalParams(),
                                  kernelParameters);
    addExtraGlobalPostLearnParams(getSrcNeuronGroup()->getName(), "_post", getTrgNeuronGroup()->getNeuronModel()->getExtraGlobalParams(),
                                  kernelParameters);

    // Finally add any weight update model extra global
    // parameters referenced in the sim to the map of kernel paramters
    addExtraGlobalPostLearnParams(groupName, "", getWUModel()->getExtraGlobalParams(), kernelParameters);

    // Synapse dynamics
    // ----------------
    // Add any of the pre or postsynaptic neuron group's extra global
    // parameters referenced in the sim code to the map of kernel parameters
    addExtraGlobalSynapseDynamicsParams(getSrcNeuronGroup()->getName(), "_pre", getSrcNeuronGroup()->getNeuronModel()->getExtraGlobalParams(),
                                        kernelParameters);
    addExtraGlobalSynapseDynamicsParams(getSrcNeuronGroup()->getName(), "_post", getTrgNeuronGroup()->getNeuronModel()->getExtraGlobalParams(),
                                        kernelParameters);

    // Finally add any weight update model extra global
    // parameters referenced in the sim to the map of kernel paramters
    addExtraGlobalSynapseDynamicsParams(groupName, "", getWUModel()->getExtraGlobalParams(), kernelParameters);

}

void SynapseGroup::addExtraGlobalSimParams(const std::string &groupName, const std::string &suffix, const NewModels::Base::StringPairVec &extraGlobalParameters,
                                           std::map<std::string, std::string> &kernelParameters) const
{
    // Loop through list of global parameters
    for(auto const &p : extraGlobalParameters) {
        std::string pnamefull = p.first + groupName;
        if (kernelParameters.find(pnamefull) == kernelParameters.end()) {
            // parameter wasn't registered yet - is it used?
            if (getWUModel()->getSimCode().find("$(" + p.first + suffix + ")") != string::npos
                || getWUModel()->getEventCode().find("$(" + p.first + suffix + ")") != string::npos
                || getWUModel()->getEventThresholdConditionCode().find("$(" + p.first + suffix + ")") != string::npos) {
                kernelParameters.insert(pair<string, string>(pnamefull, p.second));
            }
        }
    }
}

void SynapseGroup::addExtraGlobalPostLearnParams(const std::string &groupName, const std::string &suffix, const NewModels::Base::StringPairVec &extraGlobalParameters,
                                                 std::map<std::string, std::string> &kernelParameters) const
{
    // Loop through list of global parameters
    for(auto const &p : extraGlobalParameters) {
        std::string pnamefull = p.first + groupName;
        if (kernelParameters.find(pnamefull) == kernelParameters.end()) {
            // parameter wasn't registered yet - is it used?
            if (getWUModel()->getLearnPostCode().find("$(" + p.first + suffix) != string::npos) {
                kernelParameters.insert(pair<string, string>(pnamefull, p.second));
            }
        }
    }
}

void SynapseGroup::addExtraGlobalSynapseDynamicsParams(const std::string &groupName, const std::string &suffix, const NewModels::Base::StringPairVec &extraGlobalParameters,
                                                       std::map<std::string, std::string> &kernelParameters) const
{
    // Loop through list of global parameters
    for(auto const &p : extraGlobalParameters) {
        std::string pnamefull = p.first + groupName;
        if (kernelParameters.find(pnamefull) == kernelParameters.end()) {
            // parameter wasn't registered yet - is it used?
            if (getWUModel()->getSynapseDynamicsCode().find("$(" + p.first + suffix) != string::npos) {
                kernelParameters.insert(pair<string, string>(pnamefull, p.second));
            }
        }
    }
}

std::string SynapseGroup::getOffsetPre() const
{
    return getSrcNeuronGroup()->isDelayRequired()
        ? "(delaySlot * " + to_string(getSrcNeuronGroup()->getNumNeurons()) + ") + "
        : "";
}

std::string SynapseGroup::getOffsetPost(const std::string &devPrefix) const
{
    return getTrgNeuronGroup()->getQueueOffset(devPrefix);
}