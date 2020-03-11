#include <stack>
#include "feature_visitor.h"
#include "touch_extractor.h"

using namespace tvm;
using namespace tir;
using namespace autotvm;


class ASTVisitor :
      public StmtExprVisitor {
 public:
  explicit ASTVisitor(std::function<void(const ObjectRef&)> f, std::function<void(const ObjectRef&)> e) : f_(f), e_(e) {}

    void VisitStmt_(const ForNode* op) final {
        this->VisitExpr(op->loop_var);
        this->VisitExpr(op->min);
        this->VisitExpr(op->extent);
        this->VisitStmt(op->body);
    }

    void VisitExpr_(const LoadNode* op) final {
        this->VisitExpr(op->buffer_var);
        this->VisitExpr(op->index);
    }

    void VisitStmt_(const StoreNode* op) {
        this->VisitExpr(op->buffer_var);
        this->VisitExpr(op->index);
        this->VisitExpr(op->value);
    }

    void VisitExpr(const PrimExpr& node) final {
        f_(node);
        ExprVisitor::VisitExpr(node);
        e_(node);
    }

    void VisitStmt(const Stmt& node) final {
        f_(node);
        StmtVisitor::VisitStmt(node);
        e_(node);
    }

 private:
    std::function<void(const ObjectRef&)> f_;
    std::function<void(const ObjectRef&)> e_;
};

void ASTVisit(const ObjectRef& node,
                    std::function<void(const ObjectRef&)> fvisit,
                    std::function<void(const ObjectRef&)> evisit) {
    if (node.as<StmtNode>()) {
        ASTVisitor visitor(fvisit, evisit);
        visitor(Downcast<Stmt>(node));
    } else {
        ASTVisitor visitor(fvisit, evisit);
        visitor(Downcast<PrimExpr>(node));
    }
}

TVM_REGISTER_GLOBAL("autotvm.feature.GetAST")
.set_body([](TVMArgs args, TVMRetValue *ret) {
    Stmt stmt = args[0];
    PackedFunc f = args[1];
    PackedFunc e = args[2];
    ASTVisit(stmt, f, e);
});

