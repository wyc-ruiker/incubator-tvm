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

int DFSSerialize(std::shared_ptr<const Node> root,
                 std::vector<std::vector<int>> *node_list,
                 std::vector<std::vector<int>> *edge_type_list,
                 std::vector<std::string> *names) {
    std::vector<int> tmp_node_list;
    std::vector<int> tmp_edge_type_list;
    for (auto e : root->node_list) {
        int child_id = DFSSerialize(e.first, node_list, edge_type_list, names);
        tmp_node_list.push_back(child_id);
        tmp_edge_type_list.push_back(e.second);
    }

    int idx = static_cast<int>(node_list->size());
    node_list->push_back(tmp_node_list);
    edge_type_list->push_back(tmp_edge_type_list);
    names->push_back(root->name);

    return idx;
}

class GraphExtractor : public FeatureVisitor {
public:
    void Dfs(const std::shared_ptr<Node> node, std::string tab) {
        std::cout << tab << node->name << std::endl;
        for (auto e : node->node_list) {
            std::shared_ptr<Node> next_node = e.first;
            Dfs(next_node, tab + " ");
        }
    }

    void Analyze(const Stmt& stmt, const std::shared_ptr<Node> root, bool output) {
        if (output == true) std::cout << "Graph Extractor Begin!" << std::endl;
        _stack.push_back(root);
        operator()(stmt);
        if (output == true) std::cout << "Graph Extractor End!" << std::endl;
        if (output == true) Dfs(root, "");
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

void GetIRGraph(Stmt stmt, std::vector<char> *data, bool output) {
    std::shared_ptr<Node> root = std::make_shared<Node>("root");
    GraphExtractor extractor;

    extractor.Analyze(stmt, root, output);
    std::vector<std::vector<int>> node_list;
    std::vector<std::vector<int>> edge_type_list;
    std::vector<std::string> names;
    int32_t root_id = DFSSerialize(root, &node_list, &edge_type_list, &names);

    int32_t node_size = static_cast<int32_t>(node_list.size());
    int32_t offset_node, offset_edge, offset_name;
    int32_t nbytes_node, nbytes_edge, nbytes_name;
    int32_t total_size;

    nbytes_node = nbytes_edge = nbytes_name = node_size * sizeof(int32_t); // size of every vector
    for (int i = 0; i < node_size; i++) {
        nbytes_node += node_list[i].size() * sizeof(int32_t);
        nbytes_edge += edge_type_list[i].size() * sizeof(int32_t);
        nbytes_name += names[i].size() * sizeof(char);
    }

    offset_node = sizeof(int32_t) * 5;
    offset_edge = offset_node + nbytes_node;
    offset_name = offset_edge + nbytes_edge;
    total_size = offset_name + nbytes_name;

    data->resize(static_cast<size_t>(total_size), 0);
    char *pdata = data->data();
    int32_t header[] = {root_id, node_size, offset_node, offset_edge, offset_name};

    if (output == true) {
        std::cout << "C++ Header:" << std::endl;
        std::cout << root_id << " ";
        std::cout << node_size << " ";
        std::cout << offset_node << " ";
        std::cout << offset_edge << " ";
        std::cout << offset_name << " " << std::endl;
        std::cout << "C++ node_list:" << std::endl;
        for (auto &v : node_list) {
            for (auto &e : v) {
                std::cout << e << " ";
            }
            if (v.size() != 0) std::cout << std::endl;
        }
        std::cout << "C++ names" << std::endl;
        for (auto &v : names) {
            std::cout << v << std::endl;
        }
        std::cout << "C++ output end" << std::endl;
    }

    memcpy(pdata, header, sizeof(header));
    
    int32_t ct, num;
    ct = 0;
    for (int i = 0; i < node_size; i++) {
        num = static_cast<int32_t>(node_list[i].size());
        memcpy(pdata + offset_node + sizeof(num) * i, &num, sizeof(num));
        memcpy(pdata + offset_node + sizeof(num) * node_size + ct * sizeof(int32_t), node_list[i].data(), num * sizeof(int32_t));
        ct += num;
    }

    ct = 0;
    for (int i = 0; i < node_size; i++) {
        num = static_cast<int32_t>(edge_type_list[i].size());
        memcpy(pdata + offset_edge + sizeof(num) * i, &num, sizeof(num));
        memcpy(pdata + offset_edge + sizeof(num) * node_size + ct * sizeof(int32_t), edge_type_list[i].data(), num * sizeof(int32_t));
        ct += num;
    }

    ct = 0;
    for (int i = 0; i < node_size; i++) {
    num = static_cast<int32_t>(names[i].size());
    memcpy(pdata + offset_name + sizeof(num) * i, &num, sizeof(num));
    memcpy(pdata + offset_name + sizeof(num) * node_size + ct * sizeof(int8_t),
           names[i].data(), num * sizeof(int8_t));
    ct += num;
  }
}

TVM_REGISTER_GLOBAL("autotvm.feature.GetIRGraph")
.set_body([](TVMArgs args, TVMRetValue *ret) {
  Stmt stmt = args[0];
  bool output = args[1];
  std::vector<char> data;
  GetIRGraph(stmt, &data, output);
  TVMByteArray arr;
  arr.size = data.size();
  arr.data = data.data();
  *ret = arr;
});
