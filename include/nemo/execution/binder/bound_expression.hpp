#ifndef NEMO_EXECUTION_BINDER_BOUND_EXPRESSION_HPP
#define NEMO_EXECUTION_BINDER_BOUND_EXPRESSION_HPP

#include "nemo/core/ids.hpp"
#include "nemo/core/ids.hpp"
#include "nemo/core/types.hpp"
#include "nemo/types/value.hpp"
#include <memory>
#include <string>
#include <vector>
#include <variant>

namespace nemo::execution::binder {

enum class BoundExpressionType {
    Literal,
    ColumnRef,
    Parameter,
    UnaryOp,
    BinaryOp,
    Cast,
    Function,
    Aggregate
};

class BoundExpression {
public:
    virtual ~BoundExpression() = default;
    virtual BoundExpressionType type() const = 0;
    virtual types::TypeID result_type() const = 0;
    virtual std::unique_ptr<BoundExpression> clone() const = 0;
};

class BoundLiteral : public BoundExpression {
public:
    BoundLiteral(types::Value value) : m_value(std::move(value)) {}
    BoundExpressionType type() const override { return BoundExpressionType::Literal; }
    types::TypeID result_type() const override { return m_value.type(); }
    const types::Value& value() const { return m_value; }
    std::unique_ptr<BoundExpression> clone() const override { return std::make_unique<BoundLiteral>(m_value); }
private:
    types::Value m_value;
};

class BoundColumnRef : public BoundExpression {
public:
    BoundColumnRef(TableID table_id, core::ColumnID column_id, types::TypeID type, std::uint32_t tuple_index)
        : m_table_id(table_id), m_column_id(column_id), m_type(type), m_tuple_index(tuple_index) {}
    
    BoundExpressionType type() const override { return BoundExpressionType::ColumnRef; }
    types::TypeID result_type() const override { return m_type; }
    
    TableID table_id() const { return m_table_id; }
    core::ColumnID column_id() const { return m_column_id; }
    std::uint32_t tuple_index() const { return m_tuple_index; }
    
    std::unique_ptr<BoundExpression> clone() const override { 
        return std::make_unique<BoundColumnRef>(m_table_id, m_column_id, m_type, m_tuple_index); 
    }
private:
    TableID m_table_id;
    core::ColumnID m_column_id;
    types::TypeID m_type;
    std::uint32_t m_tuple_index;
};

enum class BinaryOperatorType {
    Add, Sub, Mul, Div, Mod,
    Eq, Neq, Lt, Lte, Gt, Gte,
    And, Or, Like, Concat
};

class BoundBinaryOp : public BoundExpression {
public:
    BoundBinaryOp(BinaryOperatorType op, std::unique_ptr<BoundExpression> left, std::unique_ptr<BoundExpression> right, types::TypeID res_type)
        : m_op(op), m_left(std::move(left)), m_right(std::move(right)), m_res_type(res_type) {}
        
    BoundExpressionType type() const override { return BoundExpressionType::BinaryOp; }
    types::TypeID result_type() const override { return m_res_type; }
    
    BinaryOperatorType op() const { return m_op; }
    const BoundExpression* left() const { return m_left.get(); }
    const BoundExpression* right() const { return m_right.get(); }
    
    std::unique_ptr<BoundExpression> clone() const override {
        return std::make_unique<BoundBinaryOp>(m_op, m_left->clone(), m_right->clone(), m_res_type);
    }
private:
    BinaryOperatorType m_op;
    std::unique_ptr<BoundExpression> m_left;
    std::unique_ptr<BoundExpression> m_right;
    types::TypeID m_res_type;
};

class BoundFunction : public BoundExpression {
public:
    BoundFunction(std::string function_name, std::vector<std::unique_ptr<BoundExpression>> arguments, types::TypeID return_type)
        : m_function_name(std::move(function_name)), m_arguments(std::move(arguments)), m_return_type(return_type) {}
        
    BoundExpressionType type() const override { return BoundExpressionType::Function; }
    types::TypeID result_type() const override { return m_return_type; }
    
    const std::string& function_name() const { return m_function_name; }
    const std::vector<std::unique_ptr<BoundExpression>>& arguments() const { return m_arguments; }
    
    std::unique_ptr<BoundExpression> clone() const override {
        std::vector<std::unique_ptr<BoundExpression>> cloned_args;
        for (const auto& arg : m_arguments) {
            cloned_args.push_back(arg->clone());
        }
        return std::make_unique<BoundFunction>(m_function_name, std::move(cloned_args), m_return_type);
    }
private:
    std::string m_function_name;
    std::vector<std::unique_ptr<BoundExpression>> m_arguments;
    types::TypeID m_return_type;
};

// More bound expressions (Unary, Cast, Aggregate) can be added here...

} // namespace nemo::execution::binder

#endif // NEMO_EXECUTION_BINDER_BOUND_EXPRESSION_HPP
