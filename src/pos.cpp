#include <pos.h>
#include <pos_kernel.h>
#include <chain.h>
#include <validation.h>
#include <key.h>
#include <wallet/wallet.h>
#include <txdb.h>

std::unique_ptr<CMasternodesView> pmasternodesview; // TODO: (SS) Change to real

namespace pos {
static bool CheckStakeModifier(const CBlockIndex* pindexPrev, const CBlockHeader& blockHeader) {
    if (blockHeader.hashPrevBlock.IsNull())
        return blockHeader.stakeModifier.IsNull();

    CKeyID key;
    if (!blockHeader.ExtractMinterKey(key)) {
        LogPrintf("CheckStakeModifier: Can't extract minter key\n");
        return false;
    }

    return blockHeader.stakeModifier == pos::ComputeStakeModifier(pindexPrev->stakeModifier, key);
}

/// Check PoS signatures (PoS block hashes are signed with coinstake out pubkey)
bool CheckHeaderSignature(const CBlockHeader& blockHeader) {
    if (blockHeader.sig.empty()) {
        if (blockHeader.height == 0) {
            return true;
        }
        LogPrintf("CheckBlockSignature: Bad Block - PoS signature is empty\n");
        return false;
    }

    CPubKey recoveredPubKey{};
    if (!recoveredPubKey.RecoverCompact(blockHeader.GetHashToSign(), blockHeader.sig)) {
        LogPrintf("CheckBlockSignature: Bad Block - malformed signature\n");
        return false;
    }

    return true;
}

bool CheckProofOfStake_headerOnly(const CBlockHeader& blockHeader, const Consensus::Params& params, CMasternodesView* mnView) {
    // checking PoS kernel is faster, so check it first
    if (!CheckKernelHash(blockHeader.stakeModifier, blockHeader.nBits, (int64_t) blockHeader.GetBlockTime(), params, mnView).hashOk) {
        return false;
    }

    // TODO: (SS) check address masternode operator in mnView

    return CheckHeaderSignature(blockHeader);
}

bool CheckProofOfStake(const CBlockHeader& blockHeader, const CBlockIndex* pindexPrev, const Consensus::Params& params, CMasternodesView* mnView) {
    if (!pos::CheckStakeModifier(pindexPrev, blockHeader)) {
        return false;
    }

    return CheckProofOfStake_headerOnly(blockHeader, params, mnView);
}

unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params::PoS& params)
{
    if (params.fNoRetargeting)
        return pindexLast->nBits;

    // Limit adjustment step
    int64_t nActualTimespan = pindexLast->GetBlockTime() - nFirstBlockTime;
    if (nActualTimespan < params.nTargetTimespan/4)
        nActualTimespan = params.nTargetTimespan/4;
    if (nActualTimespan > params.nTargetTimespan*4)
        nActualTimespan = params.nTargetTimespan*4;

    // Retarget
    const arith_uint256 bnDiffLimit = UintToArith256(params.diffLimit);
    arith_uint256 bnNew;
    bnNew.SetCompact(pindexLast->nBits);
    bnNew *= nActualTimespan;
    bnNew /= params.nTargetTimespan;

    if (bnNew > bnDiffLimit)
        bnNew = bnDiffLimit;

    return bnNew.GetCompact();
}

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params::PoS& params)
{
    assert(pindexLast != nullptr);
    unsigned int nProofOfWorkLimit = UintToArith256(params.diffLimit).GetCompact();

    // Only change once per difficulty adjustment interval
    if ((pindexLast->nHeight+1) % params.DifficultyAdjustmentInterval() != 0)
    {
        if (params.fAllowMinDifficultyBlocks)
        {
            // Special difficulty rule for testnet:
            // If the new block's timestamp is more than 2* 10 minutes
            // then allow mining of a min-difficulty block.
            if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nTargetSpacing*2)
                return nProofOfWorkLimit;
            else
            {
                // Return the last non-special-min-difficulty-rules-block
                const CBlockIndex* pindex = pindexLast;
                while (pindex->pprev && pindex->nHeight % params.DifficultyAdjustmentInterval() != 0 && pindex->nBits == nProofOfWorkLimit)
                    pindex = pindex->pprev;
                return pindex->nBits;
            }
        }
        return pindexLast->nBits;
    }

    // Go back by what we want to be 14 days worth of blocks
    int nHeightFirst = pindexLast->nHeight - (params.DifficultyAdjustmentInterval()-1);
    assert(nHeightFirst >= 0);
    const CBlockIndex* pindexFirst = pindexLast->GetAncestor(nHeightFirst);
    assert(pindexFirst);

    return pos::CalculateNextWorkRequired(pindexLast, pindexFirst->GetBlockTime(), params);
}

}
