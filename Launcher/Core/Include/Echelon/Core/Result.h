// Echelon @feature Codex 16/10/2026 core Result
/*
** Result<T> value-or-error container and its void-shaped Status sibling.
*/

#pragma once

#include "Echelon/Core/OperationError.h"

#include <functional>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace echelon::core
{

template <typename T>
class Result
{
	static_assert(!std::is_void<T>::value, "Result<void> is forbidden; use Status instead");

public:
	static Result Ok(T value) { return Result(std::move(value)); }
	static Result Err(OperationError error) { return Result(std::move(error)); }

	bool HasValue() const { return m_value.has_value(); }
	explicit operator bool() const { return HasValue(); }

	// Throws std::logic_error when the result holds an error.
	T &Value()
	{
		if (!m_value) {
			throw std::logic_error("Result::Value called on an error result");
		}
		return *m_value;
	}

	const T &Value() const
	{
		if (!m_value) {
			throw std::logic_error("Result::Value called on an error result");
		}
		return *m_value;
	}

	// Throws std::logic_error when the result holds a value.
	const OperationError &Error() const
	{
		if (!m_error) {
			throw std::logic_error("Result::Error called on a successful result");
		}
		return *m_error;
	}

	// Applies transform to the stored value; an error result propagates unchanged.
	template <typename U>
	Result<U> Map(std::function<U(T &)> transform)
	{
		if (!m_value) {
			return Result<U>::Err(*m_error);
		}
		return Result<U>::Ok(transform(*m_value));
	}

private:
	Result(T value) : m_value(std::move(value)) {}
	Result(OperationError error) : m_error(std::move(error)) {}

	std::optional<T> m_value;
	std::optional<OperationError> m_error;
};

// Result<void> analogue for operations that only report failure.
class Status
{
public:
	static Status Ok() { return Status(); }
	static Status Err(OperationError error) { return Status(std::move(error)); }

	bool HasValue() const { return !m_error.has_value(); }
	explicit operator bool() const { return HasValue(); }

	// Throws std::logic_error when the status is Ok.
	const OperationError &Error() const
	{
		if (!m_error) {
			throw std::logic_error("Status::Error called on a successful status");
		}
		return *m_error;
	}

private:
	Status() = default;
	explicit Status(OperationError error) : m_error(std::move(error)) {}

	std::optional<OperationError> m_error;
};

} // namespace echelon::core
