#include <stack>
#include "feature_visitor.h"
#include "touch_extractor.h"

using namespace tvm;
using namespace autotvm;

class ASTVisitor : public IRVisitor {
 public:
    explicit ASTVisitor(std::function<void(const NodeRef&)> f,
                    std::function<void(const NodeRef&)> e) : f_(f), e_(e) {}

    void Visit_(const For *op) final {
        this->Visit(op->loop_var);
        this->Visit(op->min);
        this->Visit(op->extent);
        this->Visit(op->body);
    }

    void Visit_(const Load *op) final {
        this->Visit(op->buffer_var);
        this->Visit(op->index);
        this->Visit(op->predicate);
    }

    void Visit_(const Store *op) {
        this->Visit(op->buffer_var);
        this->Visit(op->value);
        this->Visit(op->index);
        this->Visit(op->predicate);
    }

    void Visit_(const AttrStmt *op) {
        this->Visit(op->node.as<tvm::IterVarNode>()->var);
        this->Visit(op->value);
        this->Visit(op->body);
    }

    void Visit(const NodeRef& node) final {
        f_(node);
        IRVisitor::Visit(node);
        e_(node);
    }

private:
    std::function<void(const NodeRef&)> f_, e_;
};

void ASTVisit(const NodeRef& node,
            std::function<void(const NodeRef&)> fvisit,
            std::function<void(const NodeRef&)> evisit) {
  ASTVisitor(fvisit, evisit).Visit(node);
}

TVM_REGISTER_GLOBAL("autotvm.feature.GetAST")
.set_body([](TVMArgs args, TVMRetValue *ret) {
    Stmt stmt = args[0];
    PackedFunc f = args[1];
    PackedFunc e = args[2];
    ASTVisit(stmt, f, e);
});