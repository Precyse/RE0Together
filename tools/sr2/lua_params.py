"""The parameter rule of the Lua overrides: a call of a wrapped function (coop_rules.lua) that is given a parameter of an
enclosing function and not the literal REMOTE_PLAYER calls the game's function (coop_native(f)(...)).

A parameter holds one player (a trigger callback gets the triggering human's name, the per-remote helpers of
coop_missions.lua pass one remote per call), so a wrapper must not widen it to every remote. The literal REMOTE_PLAYER keeps
widening: it stands for the remote side of a two-player script.
"""
import re
from dataclasses import dataclass, field

LITERAL = "REMOTE_PLAYER"
NATIVE_CALL = "coop_native({name})"

# Comments, strings and long strings are skipped or kept whole; everything else is an identifier or one character.
TOKEN = re.compile(
    r"--\[\[.*?\]\]|--[^\n]*|\[\[.*?\]\]|\"(?:\\.|[^\"\\\n])*\"|'(?:\\.|[^'\\\n])*'|[A-Za-z_]\w*|\.\.\.|\S", re.S
)
IDENTIFIER = re.compile(r"[A-Za-z_]\w*$")
ASSIGNED_LITERAL = re.compile(rf"\b([A-Za-z_]\w*)\s*=\s*{LITERAL}\b")
OPENERS = {"function", "if", "do", "repeat"}
CLOSERS = {"end", "until"}
BRACKETS_OPEN = {"(", "[", "{"}
BRACKETS_CLOSE = {")", "]", "}"}
ARGUMENT_DEPTH = 1


@dataclass
class Function:
    """An enclosing function: its name (empty when anonymous) and parameter names in order."""
    name: str
    parameters: list = field(default_factory=list)


@dataclass
class Call:
    """A call f(arguments): the offset of f, its name, each argument as its single token (None when it has several) and
    the functions around it, outermost first."""
    offset: int
    name: str
    arguments: list
    scope: list


def wrapped_names(rules_text):
    """The names coop_rules.lua wraps: every quoted name of the file."""
    return set(re.findall(r'"(\w+)"', rules_text))


def _tokens(text):
    return [(match.start(), match.group(0)) for match in TOKEN.finditer(text) if not match.group(0).startswith("--")]


def _arguments(tokens, open_index):
    """Each argument of the call whose '(' is tokens[open_index] as its single token, or None when it has several."""
    depth, arguments, current = 0, [], []
    for _, token in tokens[open_index:]:
        if token in BRACKETS_OPEN:
            depth += 1
            if depth == ARGUMENT_DEPTH:
                continue
        elif token in BRACKETS_CLOSE:
            depth -= 1
            if depth == 0:
                break
        elif token == "," and depth == ARGUMENT_DEPTH:
            arguments.append(current)
            current = []
            continue
        current.append(token)
    arguments.append(current)
    return [argument[0] if len(argument) == 1 else None for argument in arguments if argument]


def _function_header(tokens, index):
    """(the Function declared by the 'function' at tokens[index], index of its closing parenthesis)."""
    open_index = index + 1
    while tokens[open_index][1] != "(":
        open_index += 1
    name = "".join(token for _, token in tokens[index + 1:open_index])
    function = Function(name)
    close_index = open_index + 1
    while tokens[close_index][1] != ")":
        if IDENTIFIER.match(tokens[close_index][1]):
            function.parameters.append(tokens[close_index][1])
        close_index += 1
    return function, close_index


def _calls(text):
    """Every call of a plain name (not obj.f( or obj:f() in text, with the functions around it."""
    tokens = _tokens(text)
    blocks = []  # the open blocks, innermost last: a Function for a function body, None for any other block
    calls = []
    index = 0
    while index < len(tokens):
        offset, token = tokens[index]
        if token == "function":
            function, index = _function_header(tokens, index)
            blocks.append(function)
        elif token in OPENERS:
            blocks.append(None)
        elif token in CLOSERS:
            if blocks:
                blocks.pop()
        elif (
            IDENTIFIER.match(token)
            and index + 1 < len(tokens)
            and tokens[index + 1][1] == "("
            and (index == 0 or tokens[index - 1][1] not in (".", ":"))
        ):
            calls.append(Call(offset, token, _arguments(tokens, index + 1), [b for b in blocks if b is not None]))
        index += 1
    return calls


def _parameter_uses(call):
    """(function, parameter position) for each argument of call that is a parameter of an enclosing function, else nothing.
    The innermost function that declares the name wins."""
    uses = []
    for argument in call.arguments:
        for function in reversed(call.scope):
            if argument in function.parameters:
                uses.append((function, function.parameters.index(argument)))
                break
    return uses


def _is_rewritten(call, wrapped):
    return call.name in wrapped and LITERAL not in call.arguments and bool(_parameter_uses(call))


def rewrite(text, wrapped):
    """(text with every call of a wrapped function that is given a parameter and not REMOTE_PLAYER made on the game's
    function, number of calls changed)."""
    changed = [call for call in _calls(text) if _is_rewritten(call, wrapped)]
    for call in reversed(changed):
        end = call.offset + len(call.name)
        text = text[:call.offset] + NATIVE_CALL.format(name=call.name) + text[end:]
    return text, len(changed)


def risky_calls(text, wrapped):
    """(line, helper, argument) for each call of a function whose parameter the parameter rule keeps on one player, that
    passes REMOTE_PLAYER, or a variable assigned REMOTE_PLAYER, for that parameter. Run it on the text before rewrite."""
    calls = _calls(text)
    parameters = {}  # function name -> positions of the parameters that reach a rewritten call
    for call in calls:
        if _is_rewritten(call, wrapped):
            for function, position in _parameter_uses(call):
                parameters.setdefault(function.name, set()).add(position)
    holders = {LITERAL} | set(ASSIGNED_LITERAL.findall(text))
    risky = []
    for call in calls:
        for position in parameters.get(call.name, ()):
            if position < len(call.arguments) and call.arguments[position] in holders:
                risky.append((text.count("\n", 0, call.offset) + 1, call.name, call.arguments[position]))
    return risky
