#include "script/papyrus/fiber.h"

#include <ucontext.h>

#include <cassert>

namespace rec::script::papyrus {
namespace {
// The fiber currently executing on this thread. Maintained across every context
// switch so YieldCurrent and the trampoline always find the right one.
thread_local Fiber* t_current = nullptr;

// Thrown at the suspend point when a still-suspended fiber is destroyed, so its
// frozen C++ call chain unwinds (running every local's destructor) instead of
// being abandoned with the stack. Not a std::exception, so ordinary handlers do
// not swallow it; any catch(...) on the latent path must rethrow it.
struct FiberUnwind {};
}  // namespace

struct Fiber::Context {
  ucontext_t fiber{};
  ucontext_t resumer{};
};

Fiber::Fiber(std::function<void()> entry, std::size_t stack_bytes)
    : entry_(std::move(entry)), stack_(stack_bytes), ctx_(new Context) {}

Fiber::~Fiber() {
  // A fiber abandoned mid-suspend would leak everything its frozen stack owns, so
  // resume it once in unwind mode: the suspend point throws FiberUnwind, the stack
  // unwinds through every frame's destructors, and the entry returns.
  if (started_ && !done_) {
    aborting_ = true;
    Resume();
  }
  delete ctx_;
}

void Fiber::Trampoline() {
  Fiber* self = t_current;
  try {
    self->entry_();
  } catch (const FiberUnwind&) {
    // Torn down while suspended: the stack has unwound cleanly, nothing more to do.
  }
  self->done_ = true;
  // Returning falls through to uc_link (the resumer set in Resume), so control
  // lands back after the swapcontext that started this fiber.
}

void Fiber::Resume() {
  assert(!done_ && "Resume called on a finished fiber");
  Fiber* prev = t_current;
  t_current = this;
  if (!started_) {
    started_ = true;
    getcontext(&ctx_->fiber);
    ctx_->fiber.uc_stack.ss_sp = stack_.data();
    ctx_->fiber.uc_stack.ss_size = stack_.size();
    ctx_->fiber.uc_link = &ctx_->resumer;
    makecontext(&ctx_->fiber, &Fiber::Trampoline, 0);
  }
  // Save the resumer and switch in; returns here when the fiber yields or ends.
  swapcontext(&ctx_->resumer, &ctx_->fiber);
  t_current = prev;
}

void Fiber::Yield() {
  swapcontext(&ctx_->fiber, &ctx_->resumer);
  if (aborting_) throw FiberUnwind{};  // resumed only to tear down: unwind the stack
}

bool Fiber::YieldCurrent() {
  if (!t_current) return false;
  t_current->Yield();
  return true;
}

Fiber* Fiber::current() { return t_current; }

}  // namespace rec::script::papyrus
