/*
 * Copyright (c) 2016 Shanghai Jiao Tong University.
 *     All rights reserved.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing,
 *  software distributed under the License is distributed on an "AS
 *  IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 *  express or implied.  See the License for the specific language
 *  governing permissions and limitations under the License.
 *
 * For more about this software visit:
 *
 *      http://ipads.se.sjtu.edu.cn/projects/wukong
 *
 */

#pragma once

#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/serialization/string.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/serialization/set.hpp>
#include <boost/serialization/variant.hpp>
#include <boost/serialization/split_free.hpp>
#include <set>
#include <vector>
#include <cstring>

#include "string_server.hpp"
#include "logger2.hpp"
#include "type.hpp"

using namespace std;
using namespace boost::archive;

// defined as constexpr due to switch-case
constexpr int const_pair(int t1, int t2) { return ((t1 << 4) | t2); }

enum req_type { SPARQL_QUERY = 0, DYNAMIC_LOAD = 1, GSTORE_CHECK = 2, SPARQL_HISTORY = 3 };

enum var_type {
    known_var,
    unknown_var,
    const_var
};

// EXT = [ TYPE:16 | COL:16 ]
// EXT combine message about the col of attr_res_table and the col of result_table
// TYPE = 0 means  result_table, 1 means  attr_res_table
#define NBITS_COL  16
// Init COL value with NO_RESULT
#define NO_RESULT  ((1 << NBITS_COL) - 1)

// conversion between col and ext
int col2ext(int col, int t) { return ((t << NBITS_COL) | col); }
int ext2col(int ext) { return (ext & ((1 << NBITS_COL) - 1)); }
int ext2type(int ext) { return ((ext >> NBITS_COL) & ((1 << NBITS_COL) - 1)); }

/**
 * SPARQL Query
 */
class SPARQLQuery {
private:
    friend class boost::serialization::access;

public:
    enum SQState { SQ_PATTERN = 0, SQ_UNION, SQ_FILTER, SQ_OPTIONAL, SQ_FINAL, SQ_REPLY };

    /*
     * Indicating where is this query's patterngroup from.
     * If this query is generated by generate_union_query, then PGType is UNION.
     * If this query is generated by generate_optional_query, then PGType is OPTIONAL.
     * The PGType of queries forked from BASIC is BASIC.
     * The PGType of queries forked from UNION is BASIC.
     * The PGType of queries forked from OPTIONAL is OPTIONAL.
     */
    enum PGType { BASIC, UNION, OPTIONAL };

    class Pattern {
    private:
        friend class boost::serialization::access;

    public:
        ssid_t subject;
        ssid_t predicate;
        ssid_t object;
        dir_t  direction;
        char  pred_type;

        Pattern() { }

        Pattern(ssid_t subject, ssid_t predicate, dir_t direction, ssid_t object):
            subject(subject), predicate(predicate), object(object), direction(direction) { }

        Pattern(ssid_t subject, ssid_t predicate, ssid_t direction, ssid_t object):
            subject(subject), predicate(predicate), object(object), direction((dir_t)direction) { }

        void print_pattern() { }
    };

    class Filter {
    private:
        friend class boost::serialization::access;
        template <typename Archive>
        void serialize(Archive &ar, const unsigned int version) {
            ar & type;
            ar & arg1;
            ar & arg2;
            ar & arg3;
            ar & value;
            ar & valueArg;
        }

    public:
        enum Type {Or, And, Equal, NotEqual, Less, LessOrEqual, Greater,
                   GreaterOrEqual, Plus, Minus, Mul, Div, Not, UnaryPlus, UnaryMinus,
                   Literal, Variable, IRI, Function, ArgumentList, Builtin_str,
                   Builtin_lang, Builtin_langmatches, Builtin_datatype, Builtin_bound,
                   Builtin_sameterm, Builtin_isiri, Builtin_isblank, Builtin_isliteral,
                   Builtin_regex, Builtin_in
                  };

        Type type;
        Filter *arg1, *arg2, *arg3; /// Input arguments
        std::string value; /// The value (for constants param)
        int valueArg; /// variable ids

        /// Constructor
        Filter() : arg1(0), arg2(0), arg3(0), valueArg(0) { }

        /// Copy-Constructor
        Filter(const Filter &other)
            : type(other.type), arg1(0), arg2(0), arg3(0),
              value(other.value), valueArg(other.valueArg) {
            if (other.arg1)
                arg1 = new Filter(*other.arg1);
            if (other.arg2)
                arg2 = new Filter(*other.arg2);
            if (other.arg3)
                arg3 = new Filter(*other.arg3);
        }

        /// Destructor
        ~Filter() {
            delete arg1;
            delete arg2;
            delete arg3;
        }

        void print_filter() {
            // print info
            logstream(LOG_INFO) << "[filter]" << LOG_endl;
            logstream(LOG_INFO) << "TYPE: " << this->type << LOG_endl;
            if (this->value != "")
                logstream(LOG_INFO) << "value: " << this->value << LOG_endl;
            logstream(LOG_INFO) << "valueArg: " << this->valueArg << LOG_endl;

            if (arg1 != NULL) arg1->print_filter();
            if (arg2 != NULL) arg2->print_filter();
            if (arg3 != NULL) arg3->print_filter();

            logstream(LOG_INFO) << "[filter end]" << LOG_endl;
        }
    };

    class PatternGroup {
    private:
        friend class boost::serialization::access;

    public:
        bool parallel = false;
        vector<Pattern> patterns;
        vector<PatternGroup> unions;
        vector<Filter> filters;
        vector<PatternGroup> optional;

        // new vars appeared in this OPTIONAL PG. This PG is from the vector optional
        set<ssid_t> optional_new_vars;

        void print_group() const {
            logstream(LOG_INFO) << "patterns[" << patterns.size() << "]:" << LOG_endl;
            for (auto const &p : patterns)
                logstream(LOG_INFO) << "\t" << p.subject
                                    << "\t" << p.predicate
                                    << "\t" << p.direction
                                    << "\t" << p.object << LOG_endl;

            logstream(LOG_INFO) << "unions[" << unions.size() << "]:" << LOG_endl;
            for (auto const &g : unions)
                g.print_group();

            logstream(LOG_INFO) << "optionals[" << optional.size() << "]:" << LOG_endl;
            for (auto const &g : optional)
                g.print_group();

            // FIXME: filter
        }

        // used to calculate dst_sid
        ssid_t get_start() {
            if (this->patterns.size() > 0)
                return this->patterns[0].subject;
            else if (this->unions.size() > 0)
                return this->unions[0].get_start();
            else if (this->optional.size() > 0)
                return this->optional[0].get_start();
            else
                ASSERT(false);
            return BLANK_ID;
        }
    };

    class Order {
    private:
        friend class boost::serialization::access;
        template <typename Archive>
        void serialize(Archive &ar, const unsigned int version) {
            ar & id;
            ar & descending;
        }

    public:
        ssid_t id;  /// variable id
        bool descending;    /// desending

        Order() { }

        Order(ssid_t _id, bool _descending)
            : id(_id), descending(_descending) { }
    };

    class Result {
    private:
        friend class boost::serialization::access;

        void output_result(ostream &stream, int size, String_Server *str_server) {
            for (int i = 0; i < size; i++) {
                stream << i + 1 << ": ";
                for (int j = 0; j < col_num; j++) {
                    int id = this->get_row_col(i, j);
                    if (str_server->exist(id))
                        stream << str_server->id2str[id] << "\t";
                    else
                        stream << id << "\t";
                }

                for (int c = 0; c < this->get_attr_col_num(); c++) {
                    attr_t tmp = this->get_attr_row_col(i, c);
                    stream << tmp << "\t";
                }

                stream << endl;
            }
        }

    public:
        int col_num = 0;
        int row_num = 0;  // FIXME: vs. get_row_num()
        int attr_col_num = 0; // FIXME: why not no attr_row_num

        bool blind = false;
        int nvars = 0; // the number of variables
        vector<int> v2c_map; // from variable ID (vid) to column ID, index: vid, value: col
        vector<ssid_t> required_vars; // variables selected to return

        vector<sid_t> result_table; // result table for string IDs
        vector<attr_t> attr_res_table; // result table for others

        // OPTIONAL
        vector<bool> optional_matched_rows; // mark which rows are matched in optional block

        void clear() {
            result_table.clear();
            attr_res_table.clear();
            required_vars.clear();
        }

        /// mapping from Variable ID(var) to column ID
        /// var <=> idx <=> ext <=> col
        /// var <=> idx: var is set like -1,-2 e.g. idx is the index number of v2c_map; idx = -(vid + 1)
        /// idx <=> ext: ext store the col and result tabe type(attr_res_table or result_table); ext = v2c_map[idx]
        /// ext <=> col: ext2col and col2ext
        var_type variable_type(ssid_t vid) {
            if (vid >= 0)
                return const_var;
            else if (var2col(vid) == NO_RESULT)
                return unknown_var;
            else
                return known_var;
        }

        // get column id from vid (pattern variable)
        int var2col(ssid_t vid) {
            ASSERT(vid < 0);
            if (v2c_map.size() == 0) // init
                v2c_map.resize(nvars, NO_RESULT);

            //get idx
            int idx = - (vid + 1);
            ASSERT(idx < nvars);

            // get col
            return ext2col(v2c_map[idx]);
        }

        // add column id to vid (pattern variable)
        void add_var2col(ssid_t vid, int col, int t = SID_t) {
            ASSERT(vid < 0 && col >= 0);
            if (v2c_map.size() == 0) // init
                v2c_map.resize(nvars, NO_RESULT);
            // get index
            int idx = - (vid + 1);
            ASSERT(idx < nvars);
            // col should not be set
            ASSERT(v2c_map[idx] == NO_RESULT);
            // set ext
            v2c_map[idx] = col2ext(col, t);
        }

        // judge the column id belong to attribute result table or not

        bool is_attr_col(ssid_t vid) {
            ASSERT(vid < 0);
            if (v2c_map.size() == 0) // init
                v2c_map.resize(nvars, NO_RESULT);

            //get idx
            int idx = - (vid + 1);
            ASSERT(idx < nvars);

            // get type
            int type = ext2type(v2c_map[idx]);
            if (type == 0)
                return false;
            else
                return true;
        }

        // TODO unused set get
        // result table for string IDs
        void set_col_num(int n) { col_num = n; }

        int get_col_num() { return col_num; }

        int get_row_num() {
            if (col_num == 0) {
                // FIXME: impl get_attr_row_num()
                // it only has attribute resluts
                if (attr_col_num != 0)
                    return attr_res_table.size() / attr_col_num;
                else
                    return 0;
            }
            return result_table.size() / col_num;
        }

        sid_t get_row_col(int r, int c) {
            ASSERT(r >= 0 && c >= 0);
            return result_table[col_num * r + c];
        }

        void append_row_to(int r, vector<sid_t> &update) {
            for (int c = 0; c < col_num; c++)
                update.push_back(get_row_col(r, c));
        }

        // result table for others (e.g., integer, float, and double)
        int set_attr_col_num(int n) { attr_col_num = n; }

        int get_attr_col_num() { return  attr_col_num; }

        attr_t get_attr_row_col(int r, int c) {
            return attr_res_table[attr_col_num * r + c];
        }

        void append_attr_row_to(int r, vector<attr_t> &updated_result_table) {
            for (int c = 0; c < attr_col_num; c++)
                updated_result_table.push_back(get_attr_row_col(r, c));
        }

        // insert a blank col to result table without updating col_num and v2c_map
        void insert_blank_col(int col) {
            vector<sid_t> new_table;
            for (int i = 0; i < this->get_row_num(); i++) {
                new_table.insert(new_table.end(),
                                 this->result_table.begin() + (i * this->col_num),
                                 this->result_table.begin() + (i * this->col_num + col));
                new_table.push_back(BLANK_ID);
                new_table.insert(new_table.end(),
                                 this->result_table.begin() + (i * this->col_num + col),
                                 this->result_table.begin() + ((i + 1) * this->col_num));
            }
            this->result_table.swap(new_table);
        }

        // UNION
        void merge_union(SPARQLQuery::Result &result) {
            this->nvars = result.nvars;
            this->v2c_map.resize(this->nvars, NO_RESULT);
            this->blind = result.blind;
            this->row_num += result.row_num;
            this->attr_col_num = result.attr_col_num;
            vector<int> col_map(this->nvars, -1);  // idx: my_col, value: your_col

            for (int i = 0; i < result.v2c_map.size(); i++) {
                ssid_t vid = -1 - i;
                if (this->v2c_map[i] == NO_RESULT && result.v2c_map[i] != NO_RESULT) {
                    this->insert_blank_col(this->col_num);
                    this->add_var2col(vid, this->col_num);
                    col_map[this->col_num] = result.var2col(vid);
                    this->col_num++;
                } else if (this->v2c_map[i] != NO_RESULT && result.v2c_map[i] == NO_RESULT) {
                    col_map[this->var2col(vid)] = -1;
                } else if (this->v2c_map[i] != NO_RESULT && result.v2c_map[i] != NO_RESULT) {
                    col_map[this->var2col(vid)] = result.var2col(vid);
                }
            }

            int new_size = this->col_num * this->row_num;
            this->result_table.reserve(new_size);
            for (int i = 0; i < result.row_num; i++) {
                for (int j = 0; j < this->col_num; j++) {
                    if (col_map[j] == -1)
                        this->result_table.push_back(BLANK_ID);
                    else
                        this->result_table.push_back(result.result_table[i * result.col_num + col_map[j]]);
                }
            }
            new_size = this->attr_res_table.size() + result.attr_res_table.size();
            this->attr_res_table.reserve(new_size);
            this->attr_res_table.insert(this->attr_res_table.end(),
                                        result.attr_res_table.begin(),
                                        result.attr_res_table.end());
        }

        void append_result(SPARQLQuery::Result &result) {
            this->col_num = result.col_num;
            this->blind = result.blind;
            this->row_num += result.row_num;
            this->attr_col_num = result.attr_col_num;
            this->v2c_map = result.v2c_map;

            if (!this->blind) {
                int new_size = this->result_table.size()
                               + result.result_table.size();
                this->result_table.reserve(new_size);
                this->result_table.insert(this->result_table.end(),
                                          result.result_table.begin(),
                                          result.result_table.end());
                new_size = this->attr_res_table.size()
                           + result.attr_res_table.size();
                this->attr_res_table.reserve(new_size);
                this->attr_res_table.insert(this->attr_res_table.end(),
                                            result.attr_res_table.begin(),
                                            result.attr_res_table.end());
            }
        }

        void print_result(int row2print, String_Server *str_server) {
            logstream(LOG_INFO) << "The first " << row2print << " rows of results: " << LOG_endl;
            output_result(cout, row2print, str_server);
        }

        void dump_result(string path, int row2print, String_Server *str_server) {
            if (boost::starts_with(path, "hdfs:")) {
                wukong::hdfs &hdfs = wukong::hdfs::get_hdfs();
                wukong::hdfs::fstream ofs(hdfs, path, true);
                output_result(ofs, row2print, str_server);
                ofs.close();
            } else {
                ofstream ofs(path);
                if (!ofs.good()) {
                    logstream(LOG_INFO) << "Can't open/create output file: " << path << LOG_endl;
                } else {
                    output_result(ofs, row2print, str_server);
                    ofs.close();
                }
            }
        }
    };

    int id = -1;     // query id

    int pid = -1;    // parqnt query id
    int tid = 0;     // engine thread id (MT)

    PGType pg_type = BASIC;
    SQState state = SQ_PATTERN;
    int mt_factor = 1;  // use a single engine (thread) by default
    int priority = 0;

    // Pattern
    int pattern_step = 0;
    ssid_t local_var = 0;   // the local variable
    bool corun_enabled = false;
    int corun_step = 0;
    int fetch_step = 0;

    // UNION
    bool union_done = false;

    // OPTIONAL
    int optional_step = 0;

    int limit = -1;
    unsigned offset = 0;
    bool distinct = false;

    // ID-format triple patterns (Subject, Predicat, Direction, Object)
    PatternGroup pattern_group;
    vector<Order> orders;
    Result result;

    SPARQLQuery() { }

    // build a request by existing triple patterns and variables
    SPARQLQuery(PatternGroup g, int n) : pattern_group(g) {
        result.nvars = n;
        result.v2c_map.resize(n, NO_RESULT);
    }

    // return the current pattern
    Pattern & get_pattern() {
        ASSERT(pattern_step < pattern_group.patterns.size());
        return pattern_group.patterns[pattern_step];
    }

    // return a specific pattern
    Pattern & get_pattern(int step) {
        ASSERT(step < pattern_group.patterns.size());
        return pattern_group.patterns[step];
    }

    // shrink the query to reduce communication cost (before sending)
    void shrink_query() {
        orders.clear();
        // the first pattern indicating if this query is starting from index. It can't be removed.
        if (pattern_group.patterns.size() > 0)
            pattern_group.patterns.erase(pattern_group.patterns.begin() + 1,
                                         pattern_group.patterns.end());
        pattern_group.filters.clear();
        pattern_group.optional.clear();
        pattern_group.unions.clear();

        // discard results if does not care
        if (result.blind)
            result.clear(); // clear data but reserve metadata (e.g., #rows, #cols)
    }

    bool has_pattern() { return pattern_group.patterns.size() > 0; }

    bool has_union() { return pattern_group.unions.size() > 0; }

    bool has_optional() { return pattern_group.optional.size() > 0; }

    bool has_filter() { return pattern_group.filters.size() > 0; }

    bool done(SQState state) {
        switch (state) {
        case SQ_PATTERN:
            return (pattern_step >= pattern_group.patterns.size());
        case SQ_UNION:
            return union_done;
        case SQ_FILTER:
            // FIXME: DEAD CODE currently
            ASSERT(false);
        case SQ_OPTIONAL:
            return (optional_step >= pattern_group.optional.size());
        case SQ_FINAL:
            // FIXME: DEAD CODE currently
            ASSERT(false);
        case SQ_REPLY:
            // FIXME: DEAD CODE currently
            ASSERT(false);
        }
    }

    bool start_from_index() {
        /*
         * Wukong assumes that its planner will generate a dummy pattern to hint
         * the query should start from a certain index (i.e., predicate or type).
         * For example: ?X __PREDICATE__  ub:undergraduateDegreeFrom
         *
         * NOTE: the graph exploration does not must start from this index,
         * on the contrary, starts from another index would prune bindings MORE efficiently
         * For example, ?X P0 ?Y, ?X P1 ?Z, ...
         *
         * ?X __PREDICATE__ P1 <- // start from index vertex P1
         * ?X P0 ?Y .             // then from ?X's edge with P0
         *
         */
        if (pattern_group.patterns.size() == 0) return false;
        else if (is_tpid(pattern_group.patterns[0].subject)) {
            ASSERT(pattern_group.patterns[0].predicate == PREDICATE_ID
                   || pattern_group.patterns[0].predicate == TYPE_ID);
            return true;
        }
        return false;
    }

    void print_sparql_query() {
        logstream(LOG_INFO) << "SPARQLQuery"
                            << "[ ID=" << id << " | PID=" << pid << " | TID=" << tid << " ]"
                            << LOG_endl;
        pattern_group.print_group();
        /// TODO: print more fields
        logstream(LOG_INFO) << LOG_endl;
    }

    void print_SQState() {
        logstream(LOG_INFO) << "SPARQLQuery"
                            << "[ ID=" << id << " | PID=" << pid << " | TID=" << tid << " ]";
        switch (state) {
        case SQState::SQ_PATTERN: logstream(LOG_INFO) << "\tSQ_PATTERN" << LOG_endl; break;
        case SQState::SQ_REPLY: logstream(LOG_INFO) << "\tSQ_REPLY" << LOG_endl; break;
        case SQState::SQ_UNION: logstream(LOG_INFO) << "\tSQ_UNION" << LOG_endl; break;
        case SQState::SQ_OPTIONAL: logstream(LOG_INFO) << "\tSQ_OPTIONAL" << LOG_endl; break;
        case SQState::SQ_FILTER: logstream(LOG_INFO) << "\tSQ_FILTER" << LOG_endl; break;
        case SQState::SQ_FINAL: logstream(LOG_INFO) << "\tSQ_FINAL" << LOG_endl; break;
        default: logstream(LOG_INFO) << "\tUNKNOWN_STATE" << LOG_endl;
        }
    }

    // UNION
    void inherit_union(SPARQLQuery &r, int idx) {
        pid = r.id;
        pg_type = SPARQLQuery::PGType::UNION;
        pattern_group = r.pattern_group.unions[idx];
        if (start_from_index()
                && (global_mt_threshold * global_num_servers > 1)) {
            mt_factor = r.mt_factor;
        }
        result = r.result;
        result.blind = false;
    }

    // OPTIONAL

    // currently only count BGPs in OPTIONAL
    // [invoke] inherit_optional()
    void count_optional_new_vars(Result &r) {
        for (Pattern &p : this->pattern_group.patterns) {
            if (p.subject < 0 && r.var2col(p.subject) == NO_RESULT)
                this->pattern_group.optional_new_vars.insert(p.subject);
            if (p.predicate < 0 && r.var2col(p.predicate) == NO_RESULT)
                this->pattern_group.optional_new_vars.insert(p.predicate);
            if (p.object < 0 && r.var2col(p.object) == NO_RESULT)
                this->pattern_group.optional_new_vars.insert(p.object);
        }
    }

    // restrict patterns first
    // restrict patterns: index_to_known, known_to_known, known_to_const, const_to_known
    // [invoke] inherit_optional()
    void reorder_optional_patterns(Result &r) {
        int size = this->pattern_group.patterns.size();
        vector<Pattern> updated_patterns;
        vector<Pattern> known_to_unknown_patterns;
        vector<Pattern> const_to_unknown_patterns;
        vector<Pattern> unknown_patterns;

        for (int i = 0; i < size; i++) {
            Pattern &pattern = this->pattern_group.patterns[i];
            ssid_t start = pattern.subject;
            ssid_t pid   = pattern.predicate;
            ssid_t end   = pattern.object;
            if (is_tpid(start)) {
                ASSERT(pid == PREDICATE_ID || pid == TYPE_ID);
                if (r.var2col(end) != NO_RESULT) // index_to_known
                    updated_patterns.push_back(pattern);
                else    // index_to_unknown
                    const_to_unknown_patterns.push_back(pattern);
            } else {
                switch (const_pair(r.variable_type(start), r.variable_type(end))) {
                case const_pair(const_var, known_var):
                case const_pair(known_var, const_var):
                case const_pair(known_var, known_var):
                    // const_to_known, known_to_const, known_to_known
                    updated_patterns.push_back(pattern);
                    break;
                case const_pair(const_var, unknown_var):
                    const_to_unknown_patterns.push_back(pattern);
                    break;
                case const_pair(known_var, unknown_var):
                    known_to_unknown_patterns.push_back(pattern);
                    break;
                default:
                    unknown_patterns.push_back(pattern);
                }
            }
        }
        updated_patterns.insert(updated_patterns.end(),
                                known_to_unknown_patterns.begin(),
                                known_to_unknown_patterns.end());
        updated_patterns.insert(updated_patterns.end(),
                                const_to_unknown_patterns.begin(),
                                const_to_unknown_patterns.end());
        updated_patterns.insert(updated_patterns.end(),
                                unknown_patterns.begin(),
                                unknown_patterns.end());

        this->pattern_group.patterns.swap(updated_patterns);
    }

    void inherit_optional(SPARQLQuery &r) {
        pid = r.id;
        pg_type = SPARQLQuery::PGType::OPTIONAL;
        pattern_group = r.pattern_group.optional[r.optional_step];

        if (start_from_index()
                && (global_mt_threshold * global_num_servers > 1))
            mt_factor = r.mt_factor;

        count_optional_new_vars(r.result);
        reorder_optional_patterns(r.result);
        result = r.result;
        result.optional_matched_rows = vector<bool>(r.result.get_row_num(), true);
        result.blind = false;
    }

    void correct_optional_result(int row) {
        set<ssid_t>::iterator iter;
        for (iter = this->pattern_group.optional_new_vars.begin();
                iter != this->pattern_group.optional_new_vars.end(); iter++) {
            int col = this->result.var2col(*iter);
            if (col != NO_RESULT)
                this->result.result_table[row * this->result.col_num + col] = BLANK_ID;
        }
    }
};


/**
 * SPARQL query template
 */
class SPARQLQuery_Template {
private:
    // no serialize

public:
    SPARQLQuery::PatternGroup pattern_group;

    int nvars;  // the number of variable in triple patterns

    vector<string> ptypes_str; // the Types of random-constants
    vector<int> ptypes_pos; // the locations of random-constants

    vector<vector<sid_t>> ptypes_grp; // the candidates for random-constants

    SPARQLQuery instantiate(int seed) {
        for (int i = 0; i < ptypes_pos.size(); i++) {
            int pos = ptypes_pos[i];
            switch (pos % 4) {
            case 0:
                pattern_group.patterns[pos / 4].subject =
                    ptypes_grp[i][seed % ptypes_grp[i].size()];
                break;
            case 1:
                pattern_group.patterns[pos / 4].predicate =
                    ptypes_grp[i][seed % ptypes_grp[i].size()];
                break;
            case 3:
                pattern_group.patterns[pos / 4].object =
                    ptypes_grp[i][seed % ptypes_grp[i].size()];
                break;
            }
        }

        return SPARQLQuery(pattern_group, nvars);
    }
};


/**
 * GStore consistency checker
 */
class GStoreCheck {
private:
    friend class boost::serialization::access;
    template <typename Archive>
    void serialize(Archive &ar, const unsigned int version) {
        ar & pid;
        ar & check_ret;
        ar & index_check;
        ar & normal_check;
    }

public:
    int pid = -1;    // parent query id

    int check_ret = 0;
    bool index_check = false;
    bool normal_check = false;

    GStoreCheck() { }

    GStoreCheck(bool i, bool n) : index_check(i), normal_check(n) { }
};

/**
 * RDF data loader
 */
class RDFLoad {
private:
    friend class boost::serialization::access;
    template <typename Archive>
    void serialize(Archive &ar, const unsigned int version) {
        ar & pid;
        ar & load_dname;
        ar & load_ret;
        ar & check_dup;
    }

public:
    int pid = -1;    // parent query id

    string load_dname = "";   // the file name used to be inserted
    int load_ret = 0;
    bool check_dup = false;

    RDFLoad() { }

    RDFLoad(string s, bool b) : load_dname(s), check_dup(b) { }
};

namespace boost {
namespace serialization {
char occupied = 0;
char empty = 1;

template<class Archive>
void save(Archive &ar, const SPARQLQuery::Pattern &t, unsigned int version) {
    ar << t.subject;
    ar << t.predicate;
    ar << t.object;
    ar << t.direction;
    ar << t.pred_type;
}

template<class Archive>
void load(Archive &ar, SPARQLQuery::Pattern &t, unsigned int version) {
    ar >> t.subject;
    ar >> t.predicate;
    ar >> t.object;
    ar >> t.direction;
    ar >> t.pred_type;
}

template<class Archive>
void save(Archive &ar, const SPARQLQuery::PatternGroup &t, unsigned int version) {
    ar << t.parallel;
    ar << t.patterns;
    ar << t.optional_new_vars;  // it should not be put into the "if (t.optional.size() > 0)" block. The PG itself is from optional
    if (t.filters.size() > 0) {
        ar << occupied;
        ar << t.filters;
    } else ar << empty;

    if (t.optional.size() > 0) {
        ar << occupied;
        ar << t.optional;
    } else ar << empty;

    if (t.unions.size() > 0) {
        ar << occupied;
        ar << t.unions;
    } else ar << empty;
}

template<class Archive>
void load(Archive &ar, SPARQLQuery::PatternGroup &t, unsigned int version) {
    char temp = 2;
    ar >> t.parallel;
    ar >> t.patterns;
    ar >> t.optional_new_vars;
    ar >> temp;
    if (temp == occupied) {
        ar >> t.filters;
        temp = 2;
    }
    ar >> temp;
    if (temp == occupied) {
        ar >> t.optional;
        temp = 2;
    }
    ar >> temp;
    if (temp == occupied) {
        ar >> t.unions;
        temp = 2;
    }
}

template<class Archive>
void save(Archive &ar, const SPARQLQuery::Result &t, unsigned int version) {
    ar << t.col_num;
    ar << t.row_num;
    ar << t.attr_col_num;
    ar << t.blind;
    ar << t.nvars;
    ar << t.v2c_map;
    ar << t.optional_matched_rows;
    if (!t.blind) ar << t.required_vars;
    // attr_res_table may not be empty if result_table is empty
    if (t.result_table.size() > 0 || t.attr_res_table.size() > 0) {
        ar << occupied;
        ar << t.result_table;
        ar << t.attr_res_table;
    } else {
        ar << empty;
    }
}

template<class Archive>
void load(Archive & ar, SPARQLQuery::Result &t, unsigned int version) {
    char temp = 2;
    ar >> t.col_num;
    ar >> t.row_num;
    ar >> t.attr_col_num;
    ar >> t.blind;
    ar >> t.nvars;
    ar >> t.v2c_map;
    ar >> t.optional_matched_rows;
    if (!t.blind) ar >> t.required_vars;
    ar >> temp;
    if (temp == occupied) {
        ar >> t.result_table;
        ar >> t.attr_res_table;
    }
}

template<class Archive>
void save(Archive & ar, const SPARQLQuery &t, unsigned int version) {
    ar << t.id;
    ar << t.pid;
    ar << t.tid;
    ar << t.limit;
    ar << t.offset;
    ar << t.distinct;
    ar << t.pg_type;
    ar << t.pattern_step;
    ar << t.union_done;
    ar << t.optional_step;
    ar << t.corun_step;
    ar << t.fetch_step;
    ar << t.local_var;
    ar << t.mt_factor;
    ar << t.priority;
    ar << t.state;
    ar << t.pattern_group;
    if (t.orders.size() > 0) {
        ar << occupied;
        ar << t.orders;
    } else {
        ar << empty;
    }
    ar << t.result;
}

template<class Archive>
void load(Archive & ar, SPARQLQuery &t, unsigned int version) {
    char temp = 2;
    ar >> t.id;
    ar >> t.pid;
    ar >> t.tid;
    ar >> t.limit;
    ar >> t.offset;
    ar >> t.distinct;
    ar >> t.pg_type;
    ar >> t.pattern_step;
    ar >> t.union_done;
    ar >> t.optional_step;
    ar >> t.corun_step;
    ar >> t.fetch_step;
    ar >> t.local_var;
    ar >> t.mt_factor;
    ar >> t.priority;
    ar >> t.state;
    ar >> t.pattern_group;
    ar >> temp;
    if (temp == occupied) ar >> t.orders;
    ar >> t.result;
}

}
}

BOOST_SERIALIZATION_SPLIT_FREE(SPARQLQuery::Pattern);
BOOST_SERIALIZATION_SPLIT_FREE(SPARQLQuery::PatternGroup);
BOOST_SERIALIZATION_SPLIT_FREE(SPARQLQuery::Result);
BOOST_SERIALIZATION_SPLIT_FREE(SPARQLQuery);

// remove class information at the cost of losing auto versioning,
// which is useless currently because wukong use boost serialization to transmit data
// between endpoints running the same code.
BOOST_CLASS_IMPLEMENTATION(SPARQLQuery::Pattern, boost::serialization::object_serializable);
BOOST_CLASS_IMPLEMENTATION(SPARQLQuery::PatternGroup, boost::serialization::object_serializable);
BOOST_CLASS_IMPLEMENTATION(SPARQLQuery::Filter, boost::serialization::object_serializable);
BOOST_CLASS_IMPLEMENTATION(SPARQLQuery::Order, boost::serialization::object_serializable);
BOOST_CLASS_IMPLEMENTATION(SPARQLQuery::Result, boost::serialization::object_serializable);
BOOST_CLASS_IMPLEMENTATION(SPARQLQuery, boost::serialization::object_serializable);
BOOST_CLASS_IMPLEMENTATION(GStoreCheck, boost::serialization::object_serializable);
BOOST_CLASS_IMPLEMENTATION(RDFLoad, boost::serialization::object_serializable);

// remove object tracking information at the cost of that multiple identical objects
// may be created when an archive is loaded.
// current query data structure does not contain two identical object reference
// with the same pointer
BOOST_CLASS_TRACKING(SPARQLQuery::Pattern, boost::serialization::track_never);
BOOST_CLASS_TRACKING(SPARQLQuery::Filter, boost::serialization::track_never);
BOOST_CLASS_TRACKING(SPARQLQuery::PatternGroup, boost::serialization::track_never);
BOOST_CLASS_TRACKING(SPARQLQuery::Order, boost::serialization::track_never);
BOOST_CLASS_TRACKING(SPARQLQuery::Result, boost::serialization::track_never);
BOOST_CLASS_TRACKING(SPARQLQuery, boost::serialization::track_never);
BOOST_CLASS_TRACKING(GStoreCheck, boost::serialization::track_never);
BOOST_CLASS_TRACKING(RDFLoad, boost::serialization::track_never);

/**
 * Bundle to be sent by network, with data type labeled
 * Note this class does not use boost serialization
 */
class Bundle {
private:
    req_type type;
    string data;

public:
    Bundle() { }

    Bundle(const req_type &t, const string &d): type(t), data(d) {}

    Bundle(const SPARQLQuery &r): type(SPARQL_QUERY) {
        std::stringstream ss;
        boost::archive::binary_oarchive oa(ss);

        oa << r;
        data = ss.str();
    }

    Bundle(const RDFLoad &r): type(DYNAMIC_LOAD) {
        std::stringstream ss;
        boost::archive::binary_oarchive oa(ss);

        oa << r;
        data = ss.str();
    }

    Bundle(const GStoreCheck &r): type(GSTORE_CHECK) {
        std::stringstream ss;
        boost::archive::binary_oarchive oa(ss);

        oa << r;
        data = ss.str();
    }

    Bundle(const char *str, uint64_t sz) {
        init(str, sz);
    }

    void init(const char *str, uint64_t sz) {
        uint64_t t;
        memcpy(&t, str, sizeof(uint64_t));
        string d(str + sizeof(uint64_t), sz - sizeof(uint64_t));
        set_type((req_type)t);
        set_data(d);
    }

    req_type get_type() const {
        return type;
    }

    void set_type(req_type t) {
        type = t;
    }

    string get_data() const {
        return data;
    }

    const char *get_data_c_str() const {
        return data.c_str();
    }

    void set_data(const string &d) {
        data = d;
    }

    SPARQLQuery get_sparql_query() const {
        ASSERT(type == SPARQL_QUERY);

        std::stringstream ss;
        ss << data;

        boost::archive::binary_iarchive ia(ss);
        SPARQLQuery result;
        ia >> result;
        return result;
    }

    RDFLoad get_rdf_load() const {
        ASSERT(type == DYNAMIC_LOAD);

        std::stringstream ss;
        ss << data;

        boost::archive::binary_iarchive ia(ss);
        RDFLoad result;
        ia >> result;
        return result;
    }

    GStoreCheck get_gstore_check() const {
        ASSERT(type == GSTORE_CHECK);

        std::stringstream ss;
        ss << data;

        boost::archive::binary_iarchive ia(ss);
        GStoreCheck result;
        ia >> result;
        return result;
    }

    uint64_t bundle_size() const {
        return sizeof(uint64_t) + data.length();
    }

    uint64_t data_size() const {
        return data.length();
    }

    string to_str() const {
        char c_str[bundle_size()] = {0};
        uint64_t t = (uint64_t) type;
        memcpy(c_str, &t, sizeof(uint64_t));
        memcpy(c_str + sizeof(uint64_t), data.c_str(), data.length());
        return string(c_str, bundle_size());
    }
};
