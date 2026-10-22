#pragma once

#include <QString>

#include <functional>
#include <optional>

// Conditions for button states ("enabled when", "checked when"):
//
//   selection && !dual
//   view=details || view=list
//   (files || folders) && dest
//
// Atoms are a name with an optional =value; the caller decides what they mean
// and returns nullopt for names it does not know. ! binds tightest, then &&,
// then ||; parentheses group. Names and values are case-insensitive.
namespace Condition {

using Resolver = std::function<std::optional<bool>(const QString &name, const QString &value)>;

// An empty expression is true. On a syntax error or unknown atom: false, with
// the reason in *error.
bool evaluate(const QString &expr, const Resolver &resolve, QString *error = nullptr);

// Syntax only (atom names checked against known when given).
bool check(const QString &expr, const QStringList &knownNames, QString *error = nullptr);

} // namespace Condition
