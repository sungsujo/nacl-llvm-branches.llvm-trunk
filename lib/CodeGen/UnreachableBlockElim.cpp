//===-- UnreachableBlockElim.cpp - Remove unreachable blocks for codegen --===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass is an extremely simple version of the SimplifyCFG pass.  Its sole
// job is to delete LLVM basic blocks that are not reachable from the entry
// node.  To do this, it performs a simple depth first traversal of the CFG,
// then deletes any unvisited nodes.
//
// Note that this pass is really a hack.  In particular, the instruction
// selectors for various targets should just not generate code for unreachable
// blocks.  Until LLVM has a more systematic way of defining instruction
// selectors, however, we cannot really expect them to handle additional
// complexity.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/Passes.h"
#include "llvm/Constant.h"
#include "llvm/Instructions.h"
#include "llvm/Function.h"
#include "llvm/Pass.h"
#include "llvm/Type.h"
#include "llvm/Analysis/ProfileInfo.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Support/CFG.h"
#include "llvm/Target/TargetInstrInfo.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/SmallPtrSet.h"
using namespace llvm;

namespace {
  class UnreachableBlockElim : public FunctionPass {
    virtual bool runOnFunction(Function &F);
  public:
    static char ID; // Pass identification, replacement for typeid
    UnreachableBlockElim() : FunctionPass(ID) {}

    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.addPreserved<ProfileInfo>();
    }
  };
}
char UnreachableBlockElim::ID = 0;
INITIALIZE_PASS(UnreachableBlockElim, "unreachableblockelim",
                "Remove unreachable blocks from the CFG", false, false);

FunctionPass *llvm::createUnreachableBlockEliminationPass() {
  return new UnreachableBlockElim();
}

bool UnreachableBlockElim::runOnFunction(Function &F) {
  SmallPtrSet<BasicBlock*, 8> Reachable;

  // Mark all reachable blocks.
  for (df_ext_iterator<Function*, SmallPtrSet<BasicBlock*, 8> > I =
       df_ext_begin(&F, Reachable), E = df_ext_end(&F, Reachable); I != E; ++I)
    /* Mark all reachable blocks */;

  // Loop over all dead blocks, remembering them and deleting all instructions
  // in them.
  std::vector<BasicBlock*> DeadBlocks;
  for (Function::iterator I = F.begin(), E = F.end(); I != E; ++I)
    if (!Reachable.count(I)) {
      BasicBlock *BB = I;
      DeadBlocks.push_back(BB);
      while (PHINode *PN = dyn_cast<PHINode>(BB->begin())) {
        PN->replaceAllUsesWith(Constant::getNullValue(PN->getType()));
        BB->getInstList().pop_front();
      }
      for (succ_iterator SI = succ_begin(BB), E = succ_end(BB); SI != E; ++SI)
        (*SI)->removePredecessor(BB);
      BB->dropAllReferences();
    }

  // Actually remove the blocks now.
  ProfileInfo *PI = getAnalysisIfAvailable<ProfileInfo>();
  for (unsigned i = 0, e = DeadBlocks.size(); i != e; ++i) {
    if (PI) PI->removeBlock(DeadBlocks[i]);
    DeadBlocks[i]->eraseFromParent();
  }

  return DeadBlocks.size();
}


namespace {
  class UnreachableMachineBlockElim : public MachineFunctionPass {
    virtual bool runOnMachineFunction(MachineFunction &F);
    virtual void getAnalysisUsage(AnalysisUsage &AU) const;
    MachineModuleInfo *MMI;
  public:
    static char ID; // Pass identification, replacement for typeid
    UnreachableMachineBlockElim() : MachineFunctionPass(ID) {}
  };
}
char UnreachableMachineBlockElim::ID = 0;

static RegisterPass<UnreachableMachineBlockElim>
Y("unreachable-mbb-elimination",
  "Remove unreachable machine basic blocks");

char &llvm::UnreachableMachineBlockElimID = UnreachableMachineBlockElim::ID;

void UnreachableMachineBlockElim::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addPreserved<MachineLoopInfo>();
  AU.addPreserved<MachineDominatorTree>();
  MachineFunctionPass::getAnalysisUsage(AU);
}

bool UnreachableMachineBlockElim::runOnMachineFunction(MachineFunction &F) {
  SmallPtrSet<MachineBasicBlock*, 8> Reachable;

  MMI = getAnalysisIfAvailable<MachineModuleInfo>();
  MachineDominatorTree *MDT = getAnalysisIfAvailable<MachineDominatorTree>();
  MachineLoopInfo *MLI = getAnalysisIfAvailable<MachineLoopInfo>();

  // Mark all reachable blocks.
  for (df_ext_iterator<MachineFunction*, SmallPtrSet<MachineBasicBlock*, 8> >
       I = df_ext_begin(&F, Reachable), E = df_ext_end(&F, Reachable);
       I != E; ++I)
    /* Mark all reachable blocks */;

  // Loop over all dead blocks, remembering them and deleting all instructions
  // in them.
  std::vector<MachineBasicBlock*> DeadBlocks;
  for (MachineFunction::iterator I = F.begin(), E = F.end(); I != E; ++I) {
    MachineBasicBlock *BB = I;

    // Test for deadness.
    if (!Reachable.count(BB)) {
      DeadBlocks.push_back(BB);

      // Update dominator and loop info.
      if (MLI) MLI->removeBlock(BB);
      if (MDT && MDT->getNode(BB)) MDT->eraseNode(BB);

      while (BB->succ_begin() != BB->succ_end()) {
        MachineBasicBlock* succ = *BB->succ_begin();

        MachineBasicBlock::iterator start = succ->begin();
        while (start != succ->end() && start->isPHI()) {
          for (unsigned i = start->getNumOperands() - 1; i >= 2; i-=2)
            if (start->getOperand(i).isMBB() &&
                start->getOperand(i).getMBB() == BB) {
              start->RemoveOperand(i);
              start->RemoveOperand(i-1);
            }

          start++;
        }

        BB->removeSuccessor(BB->succ_begin());
      }
    }
  }

  // Actually remove the blocks now.
  for (unsigned i = 0, e = DeadBlocks.size(); i != e; ++i)
    DeadBlocks[i]->eraseFromParent();

  // Cleanup PHI nodes.
  for (MachineFunction::iterator I = F.begin(), E = F.end(); I != E; ++I) {
    MachineBasicBlock *BB = I;
    // Prune unneeded PHI entries.
    SmallPtrSet<MachineBasicBlock*, 8> preds(BB->pred_begin(),
                                             BB->pred_end());
    MachineBasicBlock::iterator phi = BB->begin();
    while (phi != BB->end() && phi->isPHI()) {
      for (unsigned i = phi->getNumOperands() - 1; i >= 2; i-=2)
        if (!preds.count(phi->getOperand(i).getMBB())) {
          phi->RemoveOperand(i);
          phi->RemoveOperand(i-1);
        }

      if (phi->getNumOperands() == 3) {
        unsigned Input = phi->getOperand(1).getReg();
        unsigned Output = phi->getOperand(0).getReg();

        MachineInstr* temp = phi;
        ++phi;
        temp->eraseFromParent();

        if (Input != Output)
          F.getRegInfo().replaceRegWith(Output, Input);

        continue;
      }

      ++phi;
    }
  }

  F.RenumberBlocks();

  return DeadBlocks.size();
}
