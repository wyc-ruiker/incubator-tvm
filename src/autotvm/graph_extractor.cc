#include <stack>
#include "feature_visitor.h"
#include "touch_extractor.h"

using namespace tvm;
using namespace tir;
using namespace autotvm;

typedef enum {
    AST_EDGE,
} EdgeType;

class Node {
public:
    explicit Node(tir::Var var) {
        name = var.get()->name_hint;
    }
    explicit Node(std::string node_name) : name(node_name) {}
    std::string name;
    std::vector<std::pair<std::shared_ptr<Node>, EdgeType>> node_list;
};

class IndexvarCollector: public ExprVisitor {
public:
    void Collect(PrimExpr expr) {
        this->VisitExpr(expr);
    }

    void VisitExpr_(const VarNode* op) final {
        vars.insert(op);
    }

    std::set<const VarNode*> vars;
};

class GraphExtractor : public FeatureVisitor {
public:
    void Dfs(const std::shared_ptr<Node> node, std::string tab) {
        std::cout << tab << node->name << std::endl;
        for (auto e : node->node_list) {
            std::shared_ptr<Node> next_node = e.first;
            Dfs(next_node, tab + " ");
        }
    }

    void Analyze(const Stmt& stmt, const std::shared_ptr<Node> root) {
        std::cout << "Graph Extractor Begin!" << std::endl;
        _stack.push_back(root);
        operator()(stmt);
        std::cout << "Graph Extractor End!" << std::endl;
        Dfs(root, "");
    }

private:
    bool EnterItervar_(Var var, int64_t length, AnnotationType ann_type) {
        std::shared_ptr<Node> node = std::make_shared<Node>("for");
        node->node_list.push_back({std::make_shared<Node>(var), AST_EDGE});
        _stack.back()->node_list.push_back({node, AST_EDGE});
        _stack.push_back(node);
        return true;
    }
    void ExitItervar_() {
        _stack.pop_back();
    }
    void EnterMem_(Var buffer_var, PrimExpr index) {
        std::shared_ptr<Node> node = std::make_shared<Node>(buffer_var);
        IndexvarCollector collector;
        collector.Collect(index);

        for (const VarNode* op: collector.vars) {
            node->node_list.push_back({std::make_shared<Node>(op->name_hint), AST_EDGE});
        }
        for (auto iter = _stack.rbegin(); iter != _stack.rend(); iter++) {
            if (iter->get()->name == "for") {  // attach to nearest loop father node
                iter->get()->node_list.push_back({node, AST_EDGE});
                break;
            }
        }
        _stack.push_back(node);
    }
    void ExitMem_() {
        _stack.pop_back();
    }
    std::deque<std::shared_ptr<Node>> _stack; 
};

void GetIRGraph(Stmt stmt, std::vector<char> *data) {
    std::shared_ptr<Node> root = std::make_shared<Node>("root");
    GraphExtractor extractor;
    extractor.Analyze(stmt, root);
    data->resize(1, 0);
}

TVM_REGISTER_GLOBAL("autotvm.feature.GetIRGraph")
.set_body([](TVMArgs args, TVMRetValue *ret) {
  Stmt stmt = args[0];
  std::vector<char> data;
  GetIRGraph(stmt, &data);
  TVMByteArray arr;
  arr.size = data.size();
  arr.data = data.data();
  *ret = arr;
});
