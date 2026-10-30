// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/css/parser/css_if_parser.h"

#include "third_party/blink/renderer/core/css/if_condition.h"
#include "third_party/blink/renderer/core/css/kleene_value.h"
#include "third_party/blink/renderer/core/css/media_query_exp.h"
#include "third_party/blink/renderer/core/css/parser/container_query_parser.h"
#include "third_party/blink/renderer/core/css/parser/css_supports_parser.h"
#include "third_party/blink/renderer/core/css/parser/css_tokenizer.h"
#include "third_party/blink/renderer/core/css/properties/css_parsing_utils.h"
#include "third_party/blink/renderer/platform/runtime_enabled_features.h"

namespace blink {

using css_parsing_utils::AtIdent;
using css_parsing_utils::ConsumeIfIdent;

namespace {

// FlyWeb: the 128's media and container query parsers work on ranges, not on
// streams. The contents of media() and style() are taken as a range, with the
// token offsets re-tokenized from their text (as CSSParserImpl does for the
// @media prelude); the query must consume the whole block.
template <typename ParseFunc>
const MediaQueryExpNode* ConsumeBlockContentsAsRange(
    CSSParserTokenStream& stream,
    ParseFunc parse) {
  stream.EnsureLookAhead();
  wtf_size_t start = stream.LookAheadOffset();
  CSSParserTokenRange range = stream.ConsumeUntilPeekedTypeIs<>();
  stream.EnsureLookAhead();
  wtf_size_t end = stream.LookAheadOffset();
  String text = stream.StringRangeAt(start, end - start).ToString();
  Vector<wtf_size_t, 32> raw_offsets =
      CSSTokenizer(text).TokenizeToEOFWithOffsets().second;
  CSSParserTokenOffsets offsets(range.RemainingSpan(), std::move(raw_offsets),
                                text);
  const MediaQueryExpNode* node = parse(range, offsets);
  range.ConsumeWhitespace();
  if (!node || !range.AtEnd()) {
    return nullptr;
  }
  return node;
}

}  // namespace

CSSIfParser::CSSIfParser(const CSSParserContext& context)
    : container_query_parser_(ContainerQueryParser(context)),
      media_query_parser_(MediaQueryParser::kMediaQuerySetParser,
                          kHTMLStandardMode,
                          context.GetExecutionContext(),
                          MediaQueryParser::SyntaxLevel::kLevel4),
      supports_query_parser_(CSSParserImpl(&context)) {}

// <if-test> =
//   supports( [ <supports-condition> | <ident> : <declaration-value> ] ) |
//   media( <media-feature> | <media-condition> ) |
//   style( <style-query> )
const IfCondition* CSSIfParser::ConsumeIfTest(CSSParserTokenStream& stream) {
  if (RuntimeEnabledFeatures::CSSInlineIfForSupportsQueriesEnabled() &&
      stream.Peek().GetType() == kFunctionToken &&
      stream.Peek().FunctionId() == CSSValueID::kSupports) {
    CSSParserTokenStream::RestoringBlockGuard guard(stream);
    stream.ConsumeWhitespace();
    CSSSupportsParser::Result supports_parsing_result =
        CSSSupportsParser::ConsumeSupportsCondition(stream,
                                                    supports_query_parser_);
    if (supports_parsing_result != CSSSupportsParser::Result::kParseFailure) {
      guard.Release();
      stream.ConsumeWhitespace();
      bool result =
          (supports_parsing_result == CSSSupportsParser::Result::kSupported);
      return MakeGarbageCollected<IfTestSupports>(result);
    }
    if (stream.Peek().GetType() == kIdentToken &&
        supports_query_parser_.ConsumeSupportsDeclaration(stream) &&
        guard.Release()) {
      stream.ConsumeWhitespace();
      return MakeGarbageCollected<IfTestSupports>(true);
    }
  }
  if (RuntimeEnabledFeatures::CSSInlineIfForMediaQueriesEnabled() &&
      stream.Peek().GetType() == kFunctionToken &&
      stream.Peek().FunctionId() == CSSValueID::kMedia) {
    CSSParserTokenStream::RestoringBlockGuard guard(stream);
    stream.ConsumeWhitespace();
    // As in Chromium 137, <media-condition> first and then <media-feature>
    // (MediaQueryParser::ConsumeIfMediaTest).
    if (const MediaQueryExpNode* query = ConsumeBlockContentsAsRange(
            stream, [this](CSSParserTokenRange& range,
                           const CSSParserTokenOffsets& offsets) {
              return media_query_parser_.ConsumeIfMediaTest(range, offsets);
            })) {
      guard.Release();
      stream.ConsumeWhitespace();
      return MakeGarbageCollected<IfTestMedia>(query);
    }
  }
  if (stream.Peek().GetType() == kFunctionToken &&
      stream.Peek().FunctionId() == CSSValueID::kStyle) {
    CSSParserTokenStream::RestoringBlockGuard guard(stream);
    stream.ConsumeWhitespace();
    if (const MediaQueryExpNode* query = ConsumeBlockContentsAsRange(
            stream, [this](CSSParserTokenRange& range,
                           const CSSParserTokenOffsets& offsets) {
              return container_query_parser_.ConsumeIfStyleQuery(range,
                                                                 offsets);
            })) {
      guard.Release();
      stream.ConsumeWhitespace();
      return MakeGarbageCollected<IfTestStyle>(
          MediaQueryExpNode::Function(query, AtomicString("style")));
    }
  }
  return nullptr;
}

// <boolean-expr-group> = <if-test> | ( <boolean-expr[ <if-test> ]> ) |
// <general-enclosed>
// https://drafts.csswg.org/css-values-5/#typedef-boolean-expr
const IfCondition* CSSIfParser::ConsumeBooleanExprGroup(
    CSSParserTokenStream& stream) {
  // <if-test> = media( <media-query> ) | style( <style-query> )
  const IfCondition* result = ConsumeIfTest(stream);
  if (result) {
    return result;
  }

  // ( <boolean-expr[ <test> ]> )
  if (stream.Peek().GetType() == kLeftParenthesisToken) {
    CSSParserTokenStream::RestoringBlockGuard guard(stream);
    stream.ConsumeWhitespace();
    result = ConsumeBooleanExpr(stream);
    if (result && stream.AtEnd()) {
      guard.Release();
      stream.ConsumeWhitespace();
      return result;
    }
  }

  // <general-enclosed> (FlyWeb: the 128's parser takes a range).
  if (stream.Peek().GetType() == kLeftParenthesisToken ||
      stream.Peek().GetType() == kFunctionToken) {
    CSSParserTokenRange range = stream.ConsumeComponentValue();
    const MediaQueryExpNode* general_enclosed =
        media_query_parser_.ConsumeGeneralEnclosed(range);
    stream.ConsumeWhitespace();
    if (general_enclosed) {
      return MakeGarbageCollected<IfConditionUnknown>(
          To<MediaQueryUnknownExpNode>(general_enclosed)->ToString());
    }
  }

  return nullptr;
}

// <boolean-expr[ <if-test> ]> = not <boolean-expr-group> | <boolean-expr-group>
//                            [ [ and <boolean-expr-group> ]*
//                            | [ or <boolean-expr-group> ]* ]
// https://drafts.csswg.org/css-values-5/#typedef-boolean-expr
const IfCondition* CSSIfParser::ConsumeBooleanExpr(
    CSSParserTokenStream& stream) {
  if (ConsumeIfIdent(stream, "not")) {
    return IfCondition::Not(ConsumeBooleanExprGroup(stream));
  }

  const IfCondition* result = ConsumeBooleanExprGroup(stream);

  if (AtIdent(stream.Peek(), "and")) {
    while (ConsumeIfIdent(stream, "and")) {
      result = IfCondition::And(result, ConsumeBooleanExprGroup(stream));
    }
  } else if (AtIdent(stream.Peek(), "or")) {
    while (ConsumeIfIdent(stream, "or")) {
      result = IfCondition::Or(result, ConsumeBooleanExprGroup(stream));
    }
  }

  return result;
}

// <if-condition> = <boolean-expr[ <if-test> ]> | else
// https://drafts.csswg.org/css-values-5/#if-notation
const IfCondition* CSSIfParser::ConsumeIfCondition(
    CSSParserTokenStream& stream) {
  // else
  if (stream.Peek().Id() == CSSValueID::kElse) {
    stream.ConsumeIncludingWhitespace();
    return MakeGarbageCollected<IfConditionElse>();
  }
  // <boolean-expr[ <if-test> ]>
  return ConsumeBooleanExpr(stream);
}

}  // namespace blink
