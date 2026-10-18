#pragma once

#include "python_bindings/binding_core.h"

#include <optional>

#include "core/model/sequence/isequence_stream.h"

namespace python_bindings {

class IterableSequenceStream : public model::ISequenceStream {
public:
    explicit IterableSequenceStream(nb::iterable sequence);

    bool HasNext() override;
    model::TimedEventSet GetNext() override;

private:
    void Initialize();
    nb::iterable sequence_;
    nb::iterator iterator_;
    size_t index_ = 0;
    std::optional<bool> expect_explicit_timestamp_;
    bool initialized_ = false;
};

}  // namespace python_bindings
