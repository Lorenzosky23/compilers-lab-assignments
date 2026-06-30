//=============================================================================
// Loop Fusion Pass - Middle-End Optimization
//=============================================================================
// Questo passo implementa la Loop Fusion, un'ottimizzazione mirata a migliorare
// la località spaziale e temporale dei dati nella Cache L1.
// L'algoritmo unisce i corpi di due cicli adiacenti con lo stesso numero di
// iterazioni e flusso di controllo equivalente, a patto che non vi siano
// dipendenze a distanza negativa (Hazard RAW) che ne romperebbero la semantica.
//=============================================================================

#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/DependenceAnalysis.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Constants.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"

using namespace llvm;

#define DEBUG_TYPE "my-loop-fusion"

int loop_counter; 

namespace {

// Funzione di utilità per stampare i nomi dei blocchi nel debug
void printBlock(std::string s, BasicBlock *BB) {
    outs() << s << ": ";
    if (BB) BB->printAsOperand(outs(), false);
    else outs() << "null";
    outs() << "\n";
}

//=============================================================================
// UTILITIES TOPOLOGICHE E CUSTOM GUARD EXTRACTOR
//=============================================================================

// [PATCH TEST 2]: L'API nativa L->getLoopGuardBranch() fallisce se il blocco ha predecessori multipli.
// Questa funzione ispeziona manualmente l'albero per trovare l'IF precedente al loop.
BranchInst* getCustomGuard(Loop *L) {
    if (BranchInst *Guard = L->getLoopGuardBranch()) return Guard;
    
    BasicBlock *Preheader = L->getLoopPreheader();
    if (Preheader && Preheader->getSinglePredecessor()) {
        BasicBlock *Pred = Preheader->getSinglePredecessor();
        if (BranchInst *BI = dyn_cast<BranchInst>(Pred->getTerminator())) {
            if (BI->isConditional()) {
                // FIX TEST 1: Un vero blocco guardia valuta la condizione d'ingresso.
                // Se il blocco contiene Nodi PHI (come fa l'header del loop precedente
                // nel caso dei loop non protetti), allora NON è una guardia!
                if (!isa<PHINode>(Pred->begin())) {
                    return BI;
                }
            }
        }
    }
    return nullptr;
}

// Ottiene il blocco di Bypass (il ramo preso quando la guardia valuta a Falso)
BasicBlock* getBypassBlock(BranchInst *Guard, Loop *L) {
    if (!Guard) return nullptr;
    return (Guard->getSuccessor(1) != L->getLoopPreheader()) ? Guard->getSuccessor(1) : Guard->getSuccessor(0);
}

// Verifica se due istruzioni branch (guardie) valutano la medesima condizione logica
bool areGuardsEqual(BranchInst *G1, BranchInst *G2) {
    if (G1 && G2 && G1->isConditional() && G2->isConditional()) {
        auto *icmp1 = dyn_cast<ICmpInst>(G1->getCondition());
        auto *icmp2 = dyn_cast<ICmpInst>(G2->getCondition());
        return (icmp1 && icmp2 && icmp1->isIdenticalTo(icmp2)); 
    }
    return false;
}

//=============================================================================
// REQUISITI DELLA LOOP FUSION
//=============================================================================

// REQUISITO 1: ADIACENZA E CODICE INTERMEDIO (Fix Test 4)
bool areAdjacent(Loop &L1, Loop &L2) {
    outs() << "1) Are Loops Adjacent?\n";
    
    BranchInst *G1 = getCustomGuard(&L1);
    BranchInst *G2 = getCustomGuard(&L2);
    bool blocksAdjacent = false;
    
    if (G1 && G2) {
        // Caso Guarded: il bypass di L1 deve atterrare esattamente sulla guardia di L2
        blocksAdjacent = (getBypassBlock(G1, &L1) == G2->getParent());
    } else if (!G1 && !G2) {
        // Caso Unguarded: l'uscita di L1 deve essere il preheader di L2
        blocksAdjacent = (L1.getExitBlock() == L2.getLoopPreheader());
    }

    printBlock("  L1 exit block", L1.getExitBlock());
    printBlock("  L2 preheader block", L2.getLoopPreheader());

    if (blocksAdjacent) {
        BasicBlock *TransitionBlock = G2 ? G2->getParent() : L2.getLoopPreheader();
        
        for (Instruction &I : *TransitionBlock) {
            if (isa<BranchInst>(&I)) break; 
            
            // [PATCH TEST 4]: Se l'istruzione in mezzo legge/scrive in memoria o chiama print, blocchiamo tutto!
            if (I.mayHaveSideEffects() || I.mayReadOrWriteMemory()) {
                outs() << "  -> Fallimento Adiacenza: l'istruzione " << I << " ha side-effects o usa la memoria.\n";
                return false;
            }
            
            for (Value *Op : I.operands()) {
                if (Instruction *Def = dyn_cast<Instruction>(Op)) {
                    if (L1.contains(Def)) {
                        outs() << "  -> Fallimento Adiacenza: trovata dipendenza di registri.\n";
                        return false; 
                    }
                }
            }
        }
    }
    
    if (blocksAdjacent) outs() << "=> Loop " << loop_counter << " is adjacent with Loop " << loop_counter+1 << "\n";
    else outs() << "=> Loops are NOT adjacent\n";
    
    return blocksAdjacent;
}

// REQUISITO 2: TRIP COUNT IDENTICO (Fix Test 5)
bool haveSameIteration(Loop &L1, Loop &L2, ScalarEvolution &SE) {
    outs() << "2) Trip Count Check?\n";
    const SCEV *S1 = SE.getBackedgeTakenCount(&L1);
    const SCEV *S2 = SE.getBackedgeTakenCount(&L2);
  
    if (isa<SCEVCouldNotCompute>(S1) || isa<SCEVCouldNotCompute>(S2)) {
        outs() << "  -> Cannot compute trip count for one or both loops.\n";
        return false;
    }
    
    outs()  << "  -> BackedgeTakenCount of Loop " << loop_counter   << ": S1: " << *S1 << "\n";
    outs()  << "  -> BackedgeTakenCount of Loop " << loop_counter+1 << ": S2: " << *S2 << "\n";

    bool same = (S1 == S2);
    if (same) outs() << "=> Loop " << loop_counter << " and Loop " << loop_counter+1 << " have the same iteration count\n";
    else outs() << "=> Loop " << loop_counter << " and Loop " << loop_counter+1 << " have DIFFERENT iteration counts\n";
    
    return same;
}

// REQUISITO 3: CONTROL FLOW EQUIVALENCE (CFE)
bool isControlFlowEquivalent(Loop &L1, Loop &L2, DominatorTree &DT, PostDominatorTree &PDT) {
    outs() << "3) There is the Control Flow Equivalence?\n";
    
    BranchInst *G1 = getCustomGuard(&L1);
    BranchInst *G2 = getCustomGuard(&L2);

    if (G1 && G2) {
        if (!areGuardsEqual(G1, G2)) {
            outs() << "  -> le guardie non sono semanticamente uguali \n";
            return false;
        } else {
            outs() << "  -> Guards of Loop " << loop_counter << "," << loop_counter+1 << " are equals\n";
        }
    }

    BasicBlock *L1_block = G1 ? G1->getParent() : L1.getHeader();
    BasicBlock *L2_block = G2 ? G2->getParent() : L2.getHeader();

    bool cfe = (DT.dominates(L1_block, L2_block) && PDT.dominates(L2_block, L1_block));
    
    if (cfe) outs() << "=> Loop " << loop_counter << " control flow equivalent with Loop " << loop_counter+1 << "\n";
    else outs() << "=> No control flow equivalence\n";
    
    return cfe;
}

// REQUISITO 4: ASSENZA DI DIPENDENZE A DISTANZA NEGATIVA (Fix Test 3)
bool haveNotNegativeMemoryDependencies(Loop &L1, Loop &L2, ScalarEvolution &SE) {
    outs () << "4) Do Loops have negative dependencies?\n";
    
    for (BasicBlock* BB : L1.blocks()) {
        for (Instruction &I : *BB) {
            auto *storeGEP = dyn_cast<GetElementPtrInst>(&I);
            if (!storeGEP) continue;
      
            auto *storeInst = dyn_cast<StoreInst>(storeGEP->getNextNode()); 
            if (!storeInst) continue; 

            for (auto &U : storeGEP->getPointerOperand()->uses()) {
                Instruction* user = dyn_cast<Instruction>(U.getUser());
                if (!user || !L2.contains(user)) continue; 

                auto *storeOrLoadGEP = dyn_cast<GetElementPtrInst>(user);
                if (!storeOrLoadGEP) continue;

                outs() << "   Store instruction after GEP: " << *storeGEP << "\n";
                outs() << "   Load instruction after GEP2:  " << *storeOrLoadGEP << "\n";

                const SCEV *storeSCEV = SE.getSCEVAtScope(storeGEP, &L1);
                const SCEV *storeOrLoadSCEV = SE.getSCEVAtScope(storeOrLoadGEP, &L2);
                
                if (isa<SCEVCouldNotCompute>(storeSCEV) || isa<SCEVCouldNotCompute>(storeOrLoadSCEV)) continue;

                const SCEV *Diff = SE.getMinusSCEV(storeOrLoadSCEV, storeSCEV);

                outs() << "   Normalized SCEV Store: " << *storeSCEV << "\n"; 
                outs() << "   Normalized SCEV Load:  " << *storeOrLoadSCEV << "\n";
                outs() << "   Difference SCEV Diff:  " << *Diff << "\n"; 

                const SCEV *temp = Diff; 
                const SCEVConstant *ConstDiff = dyn_cast<SCEVConstant>(temp);
                
                // Discesa Type-Safe nell'albero SCEV per LLVM 19
                while (temp && !ConstDiff) {
                    if (const auto *NAry = dyn_cast<SCEVNAryExpr>(temp)) temp = NAry->getOperand(0);
                    else if (const auto *Cast = dyn_cast<SCEVCastExpr>(temp)) temp = Cast->getOperand(0);
                    else break; 
                    ConstDiff = dyn_cast<SCEVConstant>(temp);
                }
                
                if (!ConstDiff) continue;

                int offset = ConstDiff->getValue()->getSExtValue();
                outs() << "   Offset: " << offset << "\n";
                
                const SCEVAddRecExpr *DiffRec = dyn_cast<SCEVAddRecExpr>(Diff);
                if (!DiffRec) continue;

                const SCEVConstant *ConstStep = dyn_cast<SCEVConstant>(DiffRec->getStepRecurrence(SE));
                if (!ConstStep) continue;

                int step = ConstStep->getValue()->getSExtValue();
                outs() << "   Step value: " << step << "\n";
        
                if ((step > 0 && offset > 0) || (step < 0 && offset < 0)) {
                    outs() << "-> Negative dependency found due to offset " << offset << " with step " << step << "\n";
                    return false; 
                }
            }  
        } 
    }
    outs() << "=> Loop " << loop_counter << " and " << loop_counter+1 << " have no negative dependencies \n";
    return true;
}

// Validatore Principale
bool isLoopFusionValid(Loop *L1, Loop *L2, DominatorTree &DT, PostDominatorTree &PDT, ScalarEvolution &SE) {
    BranchInst *G1 = getCustomGuard(L1);
    BranchInst *G2 = getCustomGuard(L2);
    
    outs() << "0) Loops have the guard?\n";
    if ((G1 != nullptr) == (G2 != nullptr)) {
        outs() << "=> Loop " << loop_counter << " and Loop " << loop_counter+1 << " are both equally guarded with guard: " << (G1 != nullptr) << "\n";
    } else {
        outs() << "=> Guard mismatch\n";
        return false;
    }

    if (!areAdjacent(*L1, *L2)) return false;
    if (!haveSameIteration(*L1, *L2, SE)) return false;
    if (!isControlFlowEquivalent(*L1, *L2, DT, PDT)) return false;
    if (!haveNotNegativeMemoryDependencies(*L1, *L2, SE)) return false;
    
    return true;
}

//=============================================================================
// TRASFORMAZIONE FISICA (Topology Rewiring)
//=============================================================================
void mergeLoops(Loop *L1, Loop *L2, Function &F) {
    BranchInst *guard1Inst = getCustomGuard(L1);
    BranchInst *guard2Inst = getCustomGuard(L2);

    BasicBlock *guardL1 = guard1Inst ? guard1Inst->getParent() : nullptr;
    BasicBlock *preHeaderL1 = L1->getLoopPreheader();
    BasicBlock *headerL1 = L1->getHeader();
    BasicBlock *latchL1 = L1->getLoopLatch();
    BasicBlock *firstBlockBodyL1 = headerL1->getTerminator()->getSuccessor(0);
    BasicBlock *lastBlockBodyL1 = latchL1->getSinglePredecessor();
    BasicBlock *exitingL1 = L1->getExitingBlock();

    BasicBlock *guardL2 = guard2Inst ? guard2Inst->getParent() : nullptr;
    BasicBlock *preHeaderL2 = L2->getLoopPreheader();
    BasicBlock *headerL2 = L2->getHeader();
    BasicBlock *latchL2 = L2->getLoopLatch();
    BasicBlock *firstBlockBodyL2 = headerL2->getTerminator()->getSuccessor(0);
    BasicBlock *lastBlockBodyL2 = latchL2->getSinglePredecessor();
    BasicBlock *exitL2 = L2->getExitBlock();

    outs() << "\n*** L1 BLOCKS ***\n";
    printBlock("L1 PreHeader", preHeaderL1);
    printBlock("L1 Header", headerL1);
    printBlock("L1 First Block Body", firstBlockBodyL1);
    printBlock("L1 Last Block Body", lastBlockBodyL1);
    printBlock("L1 Latch", latchL1);
    printBlock("L1 Exiting Block", exitingL1);

    outs() << "*** L2 BLOCKS ***\n";
    printBlock("L2 PreHeader", preHeaderL2);
    printBlock("L2 Header", headerL2);
    printBlock("L2 First Block Body", firstBlockBodyL2);
    printBlock("L2 Last Block Body", lastBlockBodyL2);
    printBlock("L2 Latch", latchL2);
    printBlock("L2 Exit Block", exitL2);

    PHINode *inductionVariableL1 = L1->getCanonicalInductionVariable();
    PHINode *inductionVariableL2 = L2->getCanonicalInductionVariable();
    
    if (inductionVariableL1 && inductionVariableL2) {
        outs() << "Induction Variable L1: " << *inductionVariableL1 << "\n";
        outs() << "Induction Variable L2: " << *inductionVariableL2 << "\n";
        inductionVariableL2->replaceAllUsesWith(inductionVariableL1);
        inductionVariableL2->eraseFromParent(); 
    }

    if (guardL1 && guardL2) {
        guard1Inst->setSuccessor(1, getBypassBlock(guard2Inst, L2)); 

        std::vector<Instruction*> instGuardL2toMove;
        for (Instruction &inst : *guardL2) {
            if (&inst != guard2Inst) instGuardL2toMove.push_back(&inst);
        }
        for (Instruction *inst : instGuardL2toMove) {
            if (isa<PHINode>(inst)) inst->moveBefore(getBypassBlock(guard2Inst, L2)->getFirstNonPHI());
            else inst->moveBefore(guard1Inst);
        }
    }

    std::vector<Instruction*> instPreHeaderL2toMove;
    for (Instruction &inst : *preHeaderL2) {
        if (&inst != preHeaderL2->getTerminator()) instPreHeaderL2toMove.push_back(&inst);
    }
    for (Instruction *inst : instPreHeaderL2toMove) {
        outs () << "Instruzione da spostare dal PreheaderL2: " << *inst << "\n";
        inst->moveBefore(preHeaderL1->getTerminator());
    }
    preHeaderL2->replaceSuccessorsPhiUsesWith(preHeaderL1);
  
    latchL2->replaceSuccessorsPhiUsesWith(latchL1);

    std::vector<Instruction*> instHeaderL2ToMove;
    for (Instruction &inst : *headerL2) {
        if (&inst != headerL2->getTerminator()) instHeaderL2ToMove.push_back(&inst);
    }
    for (Instruction *inst : instHeaderL2ToMove) {
        outs() << "Moving instruction from L2 header: " << *inst << "\n";
        if (isa<PHINode>(inst)) inst->moveBefore(headerL1->getFirstNonPHI());
        else inst->moveBefore(headerL1->getTerminator());
    }

    exitingL1->getTerminator()->setSuccessor(1, exitL2);
    lastBlockBodyL1->getTerminator()->setSuccessor(0, firstBlockBodyL2);
    lastBlockBodyL2->getTerminator()->setSuccessor(0, latchL1);

    EliminateUnreachableBlocks(F);  
}

//=============================================================================
// DRIVER DEL PASSO
//=============================================================================
struct LoopFusionPass : public PassInfoMixin<LoopFusionPass> {
    PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM) {
        LoopInfo &LI = AM.getResult<LoopAnalysis>(F);
        DominatorTree &DT = AM.getResult<DominatorTreeAnalysis>(F);
        PostDominatorTree &PDT = AM.getResult<PostDominatorTreeAnalysis>(F);
        ScalarEvolution &SE = AM.getResult<ScalarEvolutionAnalysis>(F);
        
        if (LI.empty()) {
            outs() << "Function " << F.getName() << ": no loops found in the function. \n";
            return PreservedAnalyses::all();
        }

        outs() << "\n   *** LoopFusionPass *** \n";
        outs() << "=== Function: " << F.getName() << " === \n\n";

        loop_counter = 1;  
        auto L1_it = LI.rbegin();
        if (L1_it == LI.rend()) return PreservedAnalyses::all();
        auto L2_it = std::next(L1_it);
        
        while (L2_it != LI.rend()) {
            Loop *L1 = *L1_it;
            Loop *L2 = *L2_it;

            outs() << "* Checking Loop " << loop_counter << " and Loop " << loop_counter+1 << " *\n";

            if (isLoopFusionValid(L1, L2, DT, PDT, SE)) {
                outs() << "\nLoop " << loop_counter << " and Loop " << loop_counter+1 << " can be fused\n";
                
                mergeLoops(L1, L2, F);
                
                DT.recalculate(F);  
                PDT.recalculate(F);
                SE.forgetLoop(L1); 
                LI.releaseMemory(); 
                LI.analyze(DT);

                L1_it = LI.rbegin();  
                for(int i = 1; i < loop_counter; i++) L1_it++; 
                L2_it = std::next(L1_it);

                outs() << "\nLoops fused - L2 is removed and L1 is updated. L3 is the new L2 \n\n";
            } else {
                loop_counter++;
                L1_it++;
                L2_it = std::next(L1_it);
                outs() << "\n";
            }
        }
        return PreservedAnalyses::none();
    }
};

} // namespace

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
    return {LLVM_PLUGIN_API_VERSION, "LoopFusionPass", LLVM_VERSION_STRING,
            [](PassBuilder &PB) {
                PB.registerPipelineParsingCallback(
                    [](StringRef Name, FunctionPassManager &FPM,
                       ArrayRef<PassBuilder::PipelineElement>) {
                        if (Name == "LoopFusionPass") { 
                            FPM.addPass(LoopFusionPass());
                            return true;
                        }
                        return false;
                    });
            }};
}