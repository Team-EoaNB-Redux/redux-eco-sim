#include "test.h"

#include "pdx/parser.h"

using namespace pdx;

namespace {

std::string kv(const ScriptFile& f, const Node& n) {
    return std::string(sym_view(n.key)) + op_text(n.op) + std::string(sym_view(n.value));
}

} // namespace

TEST_CASE(parser_basic_entries_and_blocks) {
    ScriptFile f;
    parse_text("\xEF\xBB\xBF# comment\nfoo = { a = 1 b = \"x y\" } bar = 2 # trailing\n", f);
    CHECK_EQ(f.errors.size(), size_t(0));
    CHECK_EQ(f.root_count, 2u);
    const Node& foo = f.root_begin()[0];
    CHECK(foo.is_block);
    CHECK_EQ(foo.child_count, 2u);
    CHECK_EQ(kv(f, f.begin(foo)[0]), std::string("a=1"));
    CHECK_EQ(kv(f, f.begin(foo)[1]), std::string("b=x y"));
    CHECK(f.begin(foo)[1].quoted);
    CHECK_EQ(kv(f, f.root_begin()[1]), std::string("bar=2"));
    CHECK_EQ(f.root_begin()[1].line, 2u);
}

TEST_CASE(parser_operators_and_compact_syntax) {
    ScriptFile f;
    parse_text("check_variable={x>10} c={y<=2 z>=3 w!=4 v==5 u<1}", f);
    CHECK_EQ(f.errors.size(), size_t(0));
    const Node& cv = f.root_begin()[0];
    CHECK_EQ(kv(f, f.begin(cv)[0]), std::string("x>10"));
    const Node& c = f.root_begin()[1];
    CHECK_EQ(kv(f, f.begin(c)[0]), std::string("y<=2"));
    CHECK_EQ(kv(f, f.begin(c)[1]), std::string("z>=3"));
    CHECK_EQ(kv(f, f.begin(c)[2]), std::string("w!=4"));
    CHECK_EQ(kv(f, f.begin(c)[3]), std::string("v=5"));
    CHECK_EQ(kv(f, f.begin(c)[4]), std::string("u<1"));
}

TEST_CASE(parser_variable_reference_words_stay_whole) {
    ScriptFile f;
    parse_text("set_variable = { controller:pm_input_amount@var:pm_temp^num = global.x@token:food?3 }", f);
    CHECK_EQ(f.errors.size(), size_t(0));
    const Node& sv = f.root_begin()[0];
    CHECK_EQ(kv(f, f.begin(sv)[0]), std::string("controller:pm_input_amount@var:pm_temp^num=global.x@token:food?3"));
}

TEST_CASE(parser_lists_tagged_blocks_and_nested_anonymous) {
    ScriptFile f;
    parse_text("list = { ENG FRA 12 } color = rgb { 1 2 3 } ab = { { x = 1 } { y = 2 } }", f);
    CHECK_EQ(f.errors.size(), size_t(0));
    const Node& list = f.root_begin()[0];
    CHECK_EQ(list.child_count, 3u);
    CHECK_EQ(sym_str(f.begin(list)[0].value), std::string("ENG"));
    CHECK(f.begin(list)[0].key == kNoSym);
    const Node& color = f.root_begin()[1];
    CHECK(color.is_block);
    CHECK_EQ(sym_str(color.value), std::string("rgb"));
    CHECK_EQ(color.child_count, 3u);
    const Node& ab = f.root_begin()[2];
    CHECK_EQ(ab.child_count, 2u);
    CHECK(f.begin(ab)[0].is_block);
    CHECK_EQ(kv(f, f.begin(f.begin(ab)[1])[0]), std::string("y=2"));
}

TEST_CASE(parser_constants_and_dates) {
    ScriptFile f;
    parse_text("@cost = 15\nidea = { cost = @cost }\n1857.1.1 = { owner = ENG }", f);
    CHECK_EQ(f.errors.size(), size_t(0));
    CHECK_EQ(f.root_count, 2u); // @cost is a definition, not an entry
    CHECK_EQ(kv(f, f.begin(f.root_begin()[0])[0]), std::string("cost=15"));
    CHECK_EQ(sym_str(f.root_begin()[1].key), std::string("1857.1.1"));
}

TEST_CASE(parser_recovers_from_errors) {
    ScriptFile f;
    parse_text("a = { b = }\n}\nc = { d = 1\n", f);
    CHECK_EQ(f.errors.size(), size_t(3)); // missing value, stray '}', unclosed block
    CHECK_EQ(f.root_count, 2u);
    const Node& a = f.root_begin()[0];
    CHECK_EQ(a.child_count, 1u);
    const Node& c = f.root_begin()[1];
    CHECK_EQ(kv(f, f.begin(c)[0]), std::string("d=1"));
}

TEST_CASE(parser_escaped_quotes) {
    ScriptFile f;
    parse_text("log = \"say \\\"hi\\\" [?x|0]\"", f);
    CHECK_EQ(f.errors.size(), size_t(0));
    CHECK_EQ(sym_str(f.root_begin()[0].value), std::string("say \"hi\" [?x|0]"));
}
