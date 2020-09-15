// Copyright (C) 2020 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#include <gtest/gtest.h>

#include "common_test_utils/test_common.hpp"
#include <string>
#include <memory>
#include <queue>

#include <ngraph/pass/manager.hpp>
#include <ngraph/function.hpp>
#include <ngraph/opsets/opset4.hpp>
#include <ngraph_ops/fully_connected.hpp>
#include <transformations/tensor_iterator_transformations/convert_ti_to_sequences.h>
#include <transformations/utils/utils.hpp>
#include <transformations/init_node_info.hpp>

#include "common_test_utils/ngraph_test_utils.hpp"

using namespace testing;
using namespace ngraph;

TEST(TransformationTests, ConvertTensorIteratorToLSTMSequence) {
    std::shared_ptr<ngraph::Function> f(nullptr), f_ref(nullptr);
    {
        auto X = std::make_shared<opset4::Parameter>(element::f32, Shape{1, 2, 16});
        auto Y = std::make_shared<opset4::Parameter>(element::f32, Shape{1, 128});
        auto Z = std::make_shared<opset4::Parameter>(element::f32, Shape{1, 128});

        auto Xi = std::make_shared<opset4::Parameter>(element::f32, Shape{1, 1, 16});
        auto Yi = std::make_shared<opset4::Parameter>(element::f32, Shape{1, 128});
        auto Zi = std::make_shared<opset4::Parameter>(element::f32, Shape{1, 128});

        // Body
        auto axis = ngraph::opset4::Constant::create(ngraph::element::i64, ngraph::Shape{1}, {1});
        auto squeeze = std::make_shared<opset4::Squeeze>(Xi, axis);

        auto w_val = std::vector<float>(512 * 16, 0);
        auto r_val = std::vector<float>(512 * 128, 0);
        auto b_val = std::vector<float>(512, 0);
        auto W = ngraph::opset4::Constant::create(ngraph::element::f32, ngraph::Shape{512, 16}, w_val);
        auto R = ngraph::opset4::Constant::create(ngraph::element::f32, ngraph::Shape{512, 128}, r_val);
        auto B = ngraph::opset4::Constant::create(ngraph::element::f32, ngraph::Shape{512}, b_val);

        auto lstm_cell = std::make_shared<opset4::LSTMCell>(squeeze, Yi, Zi, W, R, B, 128);
        auto res_1 = std::make_shared<opset4::Result>(lstm_cell);
        auto axis_unsqueeze = ngraph::opset4::Constant::create(ngraph::element::i64, ngraph::Shape{1}, {1});
        auto unsqueeze = std::make_shared<opset4::Unsqueeze>(lstm_cell, axis_unsqueeze);
        auto res_2 = std::make_shared<opset4::Result>(unsqueeze);
        auto body = std::make_shared<Function>(OutputVector{res_1, res_2},
                                               ParameterVector{Xi, Yi, Zi});

        auto tensor_iterator = std::make_shared<opset4::TensorIterator>();
        tensor_iterator->set_body(body);

        tensor_iterator->set_invariant_input(Zi, Z);
        tensor_iterator->set_sliced_input(Xi, X, 0, 1, 1, -1, 1);
        tensor_iterator->set_merged_input(Yi, Y, res_1);

        auto out0 = tensor_iterator->get_iter_value(res_1, -1);
        auto out1 = tensor_iterator->get_concatenated_slices(res_2, 0, 1, 1, -1, 1);

        auto res_ti_1 = std::make_shared<opset4::Result>(tensor_iterator->output(1));
        //auto res_ti_2 = std::make_shared<opset4::Result>(tensor_iterator->output(0));
        f = std::make_shared<ngraph::Function>(ngraph::NodeVector{res_ti_1},
                                               ngraph::ParameterVector{X, Y, Z});

        ngraph::pass::Manager m;
        m.register_pass<ngraph::pass::InitNodeInfo>();
        m.register_pass<ngraph::pass::ConvertTensorIteratorToLSTMSequence>();
        m.set_callback([](const std::shared_ptr<const Node>&) -> bool { return true; });
        m.run_passes(f);
        ASSERT_NO_THROW(check_rt_info(f));
    }

    {
        auto X = std::make_shared<opset4::Parameter>(element::f32, Shape{1, 2, 16});
        auto Y = std::make_shared<opset4::Parameter>(element::f32, Shape{1, 128});
        auto Z = std::make_shared<opset4::Parameter>(element::f32, Shape{1, 128});

        auto w_val = std::vector<float>(512 * 16, 0);
        auto r_val = std::vector<float>(512 * 128, 0);
        auto b_val = std::vector<float>(512, 0);
        auto W = ngraph::opset4::Constant::create(ngraph::element::f32, ngraph::Shape{512, 16}, w_val);
        auto R = ngraph::opset4::Constant::create(ngraph::element::f32, ngraph::Shape{512, 128}, r_val);
        auto B = ngraph::opset4::Constant::create(ngraph::element::f32, ngraph::Shape{512}, b_val);

        auto axis_1 = ngraph::opset4::Constant::create(ngraph::element::i64, ngraph::Shape{1}, {1});
        auto in_1 = std::make_shared<ngraph::opset4::Unsqueeze>(Y, axis_1);
        auto in_2 = std::make_shared<ngraph::opset4::Unsqueeze>(Z, axis_1);

        auto axis_2 = ngraph::opset4::Constant::create(ngraph::element::i64, ngraph::Shape{1}, {0});
        auto in_4 = std::make_shared<ngraph::opset4::Unsqueeze>(W, axis_2);
        auto in_5 = std::make_shared<ngraph::opset4::Unsqueeze>(R, axis_2);
        auto in_6 = std::make_shared<ngraph::opset4::Unsqueeze>(B, axis_2);

        auto seq_lengths = ngraph::opset4::Constant::create(element::i32, Shape{1}, {2});
        auto lstm_seq = std::make_shared<op::v5::LSTMSequence>(X, in_1, in_2, seq_lengths, in_4, in_5, in_6,
                                                               128, ngraph::op::RecurrentSequenceDirection::FORWARD);
        auto axis_out = ngraph::opset4::Constant::create(ngraph::element::i64, ngraph::Shape{1}, {1});
        auto out_0 = std::make_shared<ngraph::opset4::Squeeze>(lstm_seq->output(0), axis_out);
        auto out_1 = std::make_shared<ngraph::opset4::Squeeze>(lstm_seq->output(1), axis_out);
        auto out_2 = std::make_shared<ngraph::opset4::Squeeze>(lstm_seq->output(1), axis_out);
        auto res_ti_1 = std::make_shared<opset4::Result>(out_0);
        f_ref = std::make_shared<ngraph::Function>(ngraph::NodeVector{res_ti_1}, ngraph::ParameterVector{X, Y, Z});
    }

    auto res = compare_functions(f, f_ref);
    ASSERT_TRUE(res.first) << res.second;
}

TEST(TransformationTests, ConvertTensorIteratorToRNNSequence) {
    std::shared_ptr<ngraph::Function> f(nullptr), f_ref(nullptr);
    {
        auto X = std::make_shared<opset4::Parameter>(element::f32, Shape{1, 2, 16});
        auto Y = std::make_shared<opset4::Parameter>(element::f32, Shape{1, 128});

        auto Xi = std::make_shared<opset4::Parameter>(element::f32, Shape{1, 1, 16});
        auto Yi = std::make_shared<opset4::Parameter>(element::f32, Shape{1, 128});

        // Body
        auto axis = ngraph::opset4::Constant::create(ngraph::element::i64, ngraph::Shape{1}, {1});
        auto squeeze = std::make_shared<opset4::Squeeze>(Xi, axis);

        auto w_val = std::vector<float>(128 * 16, 0);
        auto r_val = std::vector<float>(128 * 128, 0);
        auto b_val = std::vector<float>(128, 0);
        auto W = ngraph::opset4::Constant::create(ngraph::element::f32, ngraph::Shape{128, 16}, w_val);
        auto R = ngraph::opset4::Constant::create(ngraph::element::f32, ngraph::Shape{128, 128}, r_val);
        auto B = ngraph::opset4::Constant::create(ngraph::element::f32, ngraph::Shape{128}, b_val);

        auto rnn_cell = std::make_shared<opset4::RNNCell>(squeeze, Yi, W, R, B, 128);
        auto res_1 = std::make_shared<opset4::Result>(rnn_cell);
        auto axis_unsqueeze = ngraph::opset4::Constant::create(ngraph::element::i64, ngraph::Shape{1}, {1});
        auto unsqueeze = std::make_shared<opset4::Unsqueeze>(rnn_cell, axis_unsqueeze);
        auto res_2 = std::make_shared<opset4::Result>(unsqueeze);
        auto body = std::make_shared<Function>(OutputVector{res_1, res_2},
                                               ParameterVector{Xi, Yi});

        auto tensor_iterator = std::make_shared<opset4::TensorIterator>();
        tensor_iterator->set_body(body);

        tensor_iterator->set_sliced_input(Xi, X, 0, 1, 1, -1, 1);
        tensor_iterator->set_merged_input(Yi, Y, res_1);

        auto out0 = tensor_iterator->get_iter_value(res_1, -1);
        auto out1 = tensor_iterator->get_concatenated_slices(res_2, 0, 1, 1, -1, 1);

        auto res_ti_1 = std::make_shared<opset4::Result>(tensor_iterator->output(1));
        //auto res_ti_2 = std::make_shared<opset4::Result>(tensor_iterator->output(0));
        f = std::make_shared<ngraph::Function>(ngraph::NodeVector{res_ti_1},
                                               ngraph::ParameterVector{X, Y});

        ngraph::pass::Manager m;
        m.register_pass<ngraph::pass::InitNodeInfo>();
        m.register_pass<ngraph::pass::ConvertTensorIteratorToRNNSequence>();
        m.set_callback([](const std::shared_ptr<const Node>&) -> bool { return true; });
        m.run_passes(f);
        ASSERT_NO_THROW(check_rt_info(f));
    }

    {
        auto X = std::make_shared<opset4::Parameter>(element::f32, Shape{1, 2, 16});
        auto Y = std::make_shared<opset4::Parameter>(element::f32, Shape{1, 128});

        auto w_val = std::vector<float>(128 * 16, 0);
        auto r_val = std::vector<float>(128 * 128, 0);
        auto b_val = std::vector<float>(128, 0);
        auto W = ngraph::opset4::Constant::create(ngraph::element::f32, ngraph::Shape{128, 16}, w_val);
        auto R = ngraph::opset4::Constant::create(ngraph::element::f32, ngraph::Shape{128, 128}, r_val);
        auto B = ngraph::opset4::Constant::create(ngraph::element::f32, ngraph::Shape{128}, b_val);

        auto axis_1 = ngraph::opset4::Constant::create(ngraph::element::i64, ngraph::Shape{1}, {1});
        auto in_1 = std::make_shared<ngraph::opset4::Unsqueeze>(Y, axis_1);

        auto axis_2 = ngraph::opset4::Constant::create(ngraph::element::i64, ngraph::Shape{1}, {0});
        auto in_3 = std::make_shared<ngraph::opset4::Unsqueeze>(W, axis_2);
        auto in_4 = std::make_shared<ngraph::opset4::Unsqueeze>(R, axis_2);
        auto in_5 = std::make_shared<ngraph::opset4::Unsqueeze>(B, axis_2);

        auto seq_lengths = ngraph::opset4::Constant::create(element::i32, Shape{1}, {2});
        auto rnn_sequence = std::make_shared<op::v5::RNNSequence>(X, in_1, seq_lengths, in_3, in_4, in_5,
                                                                  128, ngraph::op::RecurrentSequenceDirection::FORWARD);
        auto axis_out = ngraph::opset4::Constant::create(ngraph::element::i64, ngraph::Shape{1}, {1});
        auto out_0 = std::make_shared<ngraph::opset4::Squeeze>(rnn_sequence->output(0), axis_out);
        auto out_1 = std::make_shared<ngraph::opset4::Squeeze>(rnn_sequence->output(1), axis_out);
        auto res_ti_1 = std::make_shared<opset4::Result>(out_0);
        f_ref = std::make_shared<ngraph::Function>(ngraph::NodeVector{res_ti_1}, ngraph::ParameterVector{X, Y});
    }

    auto res = compare_functions(f, f_ref);
    ASSERT_TRUE(res.first) << res.second;
}

TEST(TransformationTests, ConvertTensorIteratorToGRUSequence) {
    std::shared_ptr<ngraph::Function> f(nullptr), f_ref(nullptr);
    {
        auto X = std::make_shared<opset4::Parameter>(element::f32, Shape{1, 2, 16});
        auto Y = std::make_shared<opset4::Parameter>(element::f32, Shape{1, 128});

        auto Xi = std::make_shared<opset4::Parameter>(element::f32, Shape{1, 1, 16});
        auto Yi = std::make_shared<opset4::Parameter>(element::f32, Shape{1, 128});

        // Body
        auto axis = ngraph::opset4::Constant::create(ngraph::element::i64, ngraph::Shape{1}, {1});
        auto squeeze = std::make_shared<opset4::Squeeze>(Xi, axis);

        auto w_val = std::vector<float>(384 * 16, 0);
        auto r_val = std::vector<float>(384 * 128, 0);
        auto b_val = std::vector<float>(384, 0);
        auto W = ngraph::opset4::Constant::create(ngraph::element::f32, ngraph::Shape{384, 16}, w_val);
        auto R = ngraph::opset4::Constant::create(ngraph::element::f32, ngraph::Shape{384, 128}, r_val);
        auto B = ngraph::opset4::Constant::create(ngraph::element::f32, ngraph::Shape{384}, b_val);

        auto gru_cell = std::make_shared<opset4::GRUCell>(squeeze, Yi, W, R, B, 128);
        auto res_1 = std::make_shared<opset4::Result>(gru_cell);
        auto axis_unsqueeze = ngraph::opset4::Constant::create(ngraph::element::i64, ngraph::Shape{1}, {1});
        auto unsqueeze = std::make_shared<opset4::Unsqueeze>(gru_cell, axis_unsqueeze);
        auto res_2 = std::make_shared<opset4::Result>(unsqueeze);
        auto body = std::make_shared<Function>(OutputVector{res_1, res_2},
                                               ParameterVector{Xi, Yi});

        auto tensor_iterator = std::make_shared<opset4::TensorIterator>();
        tensor_iterator->set_body(body);

        tensor_iterator->set_sliced_input(Xi, X, 0, 1, 1, -1, 1);
        tensor_iterator->set_merged_input(Yi, Y, res_1);

        auto out0 = tensor_iterator->get_iter_value(res_1, -1);
        auto out1 = tensor_iterator->get_concatenated_slices(res_2, 0, 1, 1, -1, 1);

        auto res_ti_1 = std::make_shared<opset4::Result>(tensor_iterator->output(1));
        //auto res_tRNNCelli_2 = std::make_shared<opset4::Result>(tensor_iterator->output(0));
        f = std::make_shared<ngraph::Function>(ngraph::NodeVector{res_ti_1},
                                               ngraph::ParameterVector{X, Y});

        ngraph::pass::Manager m;
        m.register_pass<ngraph::pass::InitNodeInfo>();
        m.register_pass<ngraph::pass::ConvertTensorIteratorToGRUSequence>();
        m.set_callback([](const std::shared_ptr<const Node>&) -> bool { return true; });
        m.run_passes(f);
        ASSERT_NO_THROW(check_rt_info(f));
    }

    {
        auto X = std::make_shared<opset4::Parameter>(element::f32, Shape{1, 2, 16});
        auto Y = std::make_shared<opset4::Parameter>(element::f32, Shape{1, 128});

        auto w_val = std::vector<float>(384 * 16, 0);
        auto r_val = std::vector<float>(384 * 128, 0);
        auto b_val = std::vector<float>(384, 0);
        auto W = ngraph::opset4::Constant::create(ngraph::element::f32, ngraph::Shape{384, 16}, w_val);
        auto R = ngraph::opset4::Constant::create(ngraph::element::f32, ngraph::Shape{384, 128}, r_val);
        auto B = ngraph::opset4::Constant::create(ngraph::element::f32, ngraph::Shape{384}, b_val);

        auto axis_1 = ngraph::opset4::Constant::create(ngraph::element::i64, ngraph::Shape{1}, {1});
        auto in_1 = std::make_shared<ngraph::opset4::Unsqueeze>(Y, axis_1);

        auto axis_2 = ngraph::opset4::Constant::create(ngraph::element::i64, ngraph::Shape{1}, {0});
        auto in_3 = std::make_shared<ngraph::opset4::Unsqueeze>(W, axis_2);
        auto in_4 = std::make_shared<ngraph::opset4::Unsqueeze>(R, axis_2);
        auto in_5 = std::make_shared<ngraph::opset4::Unsqueeze>(B, axis_2);

        auto seq_lengths = ngraph::opset4::Constant::create(element::i32, Shape{1}, {2});
        auto gru_sequence = std::make_shared<op::v5::GRUSequence>(X, in_1, seq_lengths, in_3, in_4, in_5,
                                                                  128, ngraph::op::RecurrentSequenceDirection::FORWARD);
        auto axis_out = ngraph::opset4::Constant::create(ngraph::element::i64, ngraph::Shape{1}, {1});
        auto out_0 = std::make_shared<ngraph::opset4::Squeeze>(gru_sequence->output(0), axis_out);
        auto out_1 = std::make_shared<ngraph::opset4::Squeeze>(gru_sequence->output(1), axis_out);
        auto res_ti_1 = std::make_shared<opset4::Result>(out_0);
        f_ref = std::make_shared<ngraph::Function>(ngraph::NodeVector{res_ti_1}, ngraph::ParameterVector{X, Y});
    }

    auto res = compare_functions(f, f_ref);
    ASSERT_TRUE(res.first) << res.second;
}