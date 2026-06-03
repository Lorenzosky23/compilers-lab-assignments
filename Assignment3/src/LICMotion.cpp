#include "llvm/IR/PassManager.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"

using namespace llvm;

class LICMotionPass : public PassInfoMixin<LICMotionPass> {
    public:
        PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM) {
            LoopInfo &LI = FAM.getResult<LoopAnalysis>(F);

            if (LI.empty()) {
                return PreservedAnalyses::all();
            }   
            return PreservedAnalyses::all();
        }
};