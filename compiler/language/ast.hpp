// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// The syntax tree: what a .one file says, with every node remembering where it came
// from. The tree only records what was written. Whether it makes sense (that a
// command's entity exists, that names are snake_case) is the checker's job.

#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <algorithm>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "language/token.hpp"

namespace one::language {

    // A name that may reach into namespaces or an entity: book, loan::checkin,
    // waitlist::signup::create.
    struct qualified_name {
        std::vector<std::string> parts;
        location where;

        std::string text() const {
            std::string joined;
            for (const auto& part : parts) {
                if (!joined.empty()) joined += "::";
                joined += part;
            }
            return joined;
        }
    };

    // expressions

    struct expression;
    using expression_ptr = std::unique_ptr<expression>;

    struct literal_expression {
        // A time is one relative to when it's read, like 7 days ago or 2 weeks from
        // now, written as its offset: -7d, +14d.
        enum class kind { string, number, time };
        kind type = kind::string;
        std::string value;
    };

    struct name_expression {
        qualified_name name;
    };

    struct member_expression {  // book.title
        expression_ptr object;
        std::string member;
    };

    struct call_expression {    // count(signup)
        expression_ptr callee;
        std::vector<expression_ptr> arguments;
    };

    struct where_expression {   // loan where returned_at == none
        expression_ptr source;
        expression_ptr condition;
    };

    struct unary_expression {   // !done, -created_at
        token_kind op = token_kind::logical_not;
        expression_ptr operand;
    };

    // [role::maintainer, role::programmer]: a list's values, given whole, as what a
    // create statement gives a list field.
    struct list_expression {
        std::vector<expression_ptr> items;
    };

    struct binary_expression {
        token_kind op = token_kind::equal;
        expression_ptr left;
        expression_ptr right;
    };

    // match relation { blocks  blocked_by … }: the value for each choice of an enum,
    // one a line, covering every choice or ending in else.
    struct match_arm {
        std::string choice;
        location where;
        expression_ptr value;
    };

    struct match_expression {
        expression_ptr subject;
        std::vector<match_arm> arms;
        expression_ptr otherwise;  // else, when the arms don't cover every choice
        location otherwise_where;
    };

    struct expression {
        location where;
        std::variant<literal_expression, name_expression, member_expression, call_expression,
                     where_expression, unary_expression, binary_expression, list_expression, match_expression>
            node;
    };

    // statements, in command and function bodies

    struct statement;

    struct require_statement {
        expression_ptr condition;
        std::string message;
    };

    struct permission_statement {  // anyone, signed_in, owner, or a permission
        qualified_name permission;
        bool by = false;  // by anyone signed in: who runs a command no role allows, said as it reads
    };

    struct clear_statement {
        std::vector<std::string> fields;
    };

    // changes cloud_project deploy_account: fields an update may change besides
    // those its forms ask for, like the ones a hand-written component sends.
    struct changes_statement {
        std::vector<std::string> fields;
    };

    struct assign_statement {
        expression_ptr target;
        expression_ptr value;
    };

    // add me to assignees, or remove me from assignees.
    struct list_statement {
        bool adds = true;
        expression_ptr value;
        std::string list;
        location list_where;
    };

    // A value given to a field of what a create statement makes: role = maintainer.
    // from 0 to 100  by 5: the numbers something may be, each as written.
    struct number_range {
        std::string from, to, by = "1";
        location where;
    };

    struct field_value {
        std::string name;
        location where;
        expression_ptr value;
        // opacity from 0 to 1: not one value but any along a slider, by a hundredth of
        // the way unless it says its step.
        std::optional<number_range> range;
        // What taking it out cuts: it and the space after it, or before it when it's
        // last on its line, or its whole line when it's alone on it.
        location cut_where;
        std::size_t cut_length = 0;
    };

    // create member { project = id  person = me }: another entity, made in the same
    // step as the command.
    struct create_statement {
        std::string entity;
        location entity_where;
        std::vector<field_value> values;
        // board::create { ... }: made as that command makes it, its body run too,
        // like a board's stages made from the preset it starts from.
        std::optional<qualified_name> command;
        std::size_t head_length = 0;  // create board, or board::create: what dispatch replaces
    };

    // dispatch board::create { project = id  name = "main" }: another command run in
    // the same step as this one, as the same person, its body and requires too. An
    // update or a delete names what it acts on with id = ..., or acts on the row of an
    // each over its entity. Its permission isn't asked again: this command vouches.
    struct dispatch_statement {
        qualified_name command;
        std::vector<field_value> values;
        std::vector<statement> lists;  // add role::contributor to roles
    };

    struct return_statement {
        expression_ptr value;
    };

    // stripe checkout { price = plan  quantity = seats  done = "/billing"  canceled = "/billing" }:
    // a call to Stripe for the customer the command is on, like an organization:
    // checkout, portal, change, or cancel, at its period's end or now.
    struct stripe_statement {
        std::string action;
        location action_where;
        std::vector<field_value> values;
        bool now = false;  // cancel now
    };

    struct if_statement {
        expression_ptr condition;
        std::vector<statement> then_body;
        std::vector<statement> else_body;
    };

    // input into stage: a value a command is sent besides its entity's fields, like
    // the stage a removed stage's issues move to. It's asked for by its forms, named
    // in its body, and never stored.
    struct input_statement {
        std::string name;
        location name_where;
        qualified_name type;
    };

    // each issue where stage == id { stage = into }: other entities changed with the
    // command, in the same step. The where picks them by a field's value, worked out
    // where the command runs; the body names the picked one's fields plainly.
    struct each_statement {
        std::string entity;
        location entity_where;
        bool other = false;  // each other link: a row of the command's own kind, named other
        expression_ptr where;
        std::vector<statement> body;
    };

    // delete each step where from == id || to == id: other entities deleted with the
    // command, in the same step, picked by a field's value, or by any of several.
    struct delete_statement {
        std::string entity;
        location entity_where;
        expression_ptr where;
    };

    struct statement {
        location where;
        std::variant<require_statement, permission_statement, clear_statement, changes_statement, assign_statement,
                     create_statement, list_statement, return_statement, if_statement, input_statement, each_statement,
                     delete_statement, dispatch_statement, stripe_statement>
            node;
        std::string text = {};  // as written, for a statement on one line, which a fix rewrites
    };

    // declarations

    struct setting {
        std::string key;
        std::string value;
        bool is_string = false;
        location where;
        std::string to;  // redirect "/install.sh" "https://www.uione.io/install.sh": where it goes
        // corners one::corners::square: the value written in full, whose last part is
        // value; empty when it's written plainly.
        std::string qualified;
        location value_where;
    };

    // import one: a library a file uses, like uione's own, which says what a project
    // block may say.
    struct import_declaration {
        std::string name;
        location where;
    };


    // A place the project runs, with what's its own there: environment staging {
    // domain "staging.example.org" ... }.
    struct environment_block {
        std::string name;
        location where;
        location end;  // its closing brace
        std::vector<setting> settings;
    };

    // at most 30 per hour: how many a caller may do in that time. On an entity it's
    // how many of it each caller may make in a day; in a project block, what every
    // command or entity has unless it says its own.
    struct at_most {
        int count = 0;
        std::string per;  // second, minute, hour or day
        location where;   // its at
        location per_where;
    };

    struct project_declaration {
        std::string name;
        std::vector<setting> settings;          // shared by every environment
        std::vector<language::at_most> at_most;  // at most 600 per minute, at most 5000 per day
        std::vector<environment_block> environments;
        std::string environment;                // the one being built, once chosen; empty with none
        location end;                           // its closing brace
    };

    // enum status { open "Open"  closed "Closed" }: choices named once, for any field
    // of this type, each written on its own line or several to a line.
    struct enum_declaration {
        std::string name;
        std::vector<std::string> choices;
        std::vector<std::string> choice_labels;  // how each is shown, or empty for its name
        std::vector<location> choice_where;
    };

    struct format_declaration {
        std::string name;
        std::string pattern;  // raw, like AAA-9999
        std::string example;  // raw, like HIS-0142
    };

    struct field {
        std::string name;
        location where;
        // A field whose type is an enum declared on its own, like status  status, is
        // given that enum's choices once the project is checked, as if they were
        // written on it, so everything after the checker sees choices either way.
        mutable std::optional<qualified_name> type;  // none when the field has choices, or no type
        mutable std::vector<std::string> choices;    // on_shelf | lent | withdrawn
        mutable std::vector<std::string> choice_labels;  // mit "MIT": how each choice is shown, or empty for its name
        mutable std::string enum_name;       // the enum its choices are from, which names them, like status
        bool list = false;                   // labels  list of label: several, each a label
        bool required = false;
        bool unique = false;
        bool key = false;
        bool searchable = false;              // body  markdown  searchable: its words kept, for find to search
        location searchable_where;
        std::optional<std::string> per;       // serial per book: counted within each book
        std::optional<std::string> after;
        // volume  number  from 0 to 100  by 5: the numbers it may hold, each as written,
        // drawn as a slider.
        using number_range = language::number_range;
        std::optional<number_range> range;
        expression_ptr initial;               // = on_shelf
        std::optional<std::string> label;     // number  serial  "#": what every screen calls it, unless one says its own
    };

    struct entity_declaration {
        std::string name;
        std::optional<language::at_most> at_most;  // at most 10 per day: made by each caller
        bool history = false;  // entity issue history: every change is kept
        // entity comment history of issue: each change to a comment is kept in its
        // issue's history, through its field pointing at the issue.
        std::optional<std::string> history_of;
        location history_of_where;
        // entity invitation invites member: when someone signs in with its email, a
        // member is made from its fields of the same names, with the member's person
        // the one signing in, and it's deleted, both at once.
        std::optional<std::string> invites;
        location invites_where;
        // entity access gives projects::member to each teams::member: each record gives
        // a role in a place to everyone in a group, like a team's role in a project.
        std::optional<qualified_name> gives;
        qualified_name gives_each;
        location gives_where;
        // readers member and public when visibility == visibility::public: who may read
        // the views of it, and of what's within it, unless a view says its own.
        std::optional<std::string> readers;
        location readers_where;
        expression_ptr public_when;
        // audit kept 1 year: a record of every command run in it and in what's
        // within it, kept that many days, on the audit records define role declares.
        int audit_days = 0;
        location audit_where;
        std::vector<field> fields;
    };

    // settings project { domain domain ... }: what a block of settings may say, each
    // line one of these fields, with a value of its type. Written in a library.
    struct settings_declaration {
        std::string name;
        std::vector<field> fields;
    };

    // What another file adds to a command with extend command: its requires, checked
    // where they're written.
    struct command_extension {
        std::string path;
        std::vector<statement> body;
    };

    struct command_declaration {
        qualified_name name;
        bool has_body = false;
        bool apart = false;  // apart from undo: kept, but never a step undo takes back (decision 0018)
        location apart_where;
        std::vector<statement> body;
        std::optional<language::at_most> at_most;
        std::vector<command_extension> extended;
    };

    // extend command projects::project::create { require ... }: requires a project adds
    // to a command another declares, like a hosted edition's limits on the open-source
    // app's commands. Lowering moves them onto that command.
    struct extend_command_declaration {
        qualified_name command;
        std::vector<statement> body;
    };

    // One value in a view: either something shown as it is (book.title), or a name
    // given a computed value (total = count(signup)).
    struct view_value {
        location where;
        std::optional<std::string> name;
        expression_ptr value;
    };

    // A list in a view: the view's rows, or, with a name, one of several lists, like
    // an issue's comments.
    struct view_each {
        location where;
        std::optional<std::string> name;    // comments = each comment ...
        bool changes = false;               // each change in issue: the changes an entity keeps
        // each issue in project: the rows whose field pointing at the view's project
        // holds it; each change in issue in project, the changes of a project's issues.
        std::vector<std::pair<std::string, location>> within;
        location where_word;              // where its where starts, and how long the
        std::size_t where_length = 0;     // condition after it runs, for a fix to say it as in
        std::vector<expression_ptr> order;  // -due_at sorts in reverse
        std::optional<int> limit;           // limit 50: only the first, once ordered
        expression_ptr source;
        expression_ptr condition;
        std::vector<view_value> rows;
        // In each user: lists of what's each person's, read for one person, like
        // payments = each billing::payment where account.created_by == user { ... }.
        std::vector<view_each> lists;
    };

    struct view_declaration {
        std::string name;
        std::optional<std::string> per;
        bool is_public = false;
        std::optional<std::string> readers;  // readers member: the people a member names may read it
        location readers_where;
        std::vector<expression_ptr> reader_people;  // readers report.author: who a field of the entity names
        // readers owner, readers auditor: only those holding these roles, of the
        // place's define role, read it; said as readers member and these.
        std::vector<std::string> reader_roles;
        std::vector<location> reader_roles_where;
        expression_ptr public_when;          // public when project.visibility == public
        // Where each of those lines is, whole, so a line that says what its entity
        // already does can be taken out.
        location readers_line, public_line;
        std::size_t readers_length = 0, public_length = 0;
        std::vector<view_value> values;
        std::vector<view_each> each;
    };

    struct role_declaration {
        std::string name;
        std::optional<std::string> per;   // role maintainer per project from member: held within a project
        std::optional<std::string> from;  // the entity that grants it
        location per_where;
        std::vector<qualified_name> permissions;
    };

    // holds projects::maintainer in each projects::project: whoever holds the role holds
    // that one too, in each of those within what the role is in, like an organization's
    // owner maintaining each of its projects.
    struct role_holding {
        qualified_name role;  // projects::maintainer
        qualified_name each;  // projects::project
        location where;
    };

    // A role each project starts with: maintainer "Maintainer"  issue::create ...
    struct default_role {
        std::string name;
        location where;
        std::string title;
        std::vector<qualified_name> permissions;
        bool services = false;  // a service account may hold it, as well as a person
        std::vector<role_holding> holds = {};  // what else it holds, in each of what's within
        int keep = 0;                          // how many of it a place keeps, at least
    };

    // roles role per project from member { ... }: the roles each project defines for
    // itself, as records of role that its people edit, each listing the commands it
    // allows; a member gives its person the role it points at. The block holds the
    // roles each project starts with.
    struct roles_declaration {
        std::string entity;  // role
        location entity_where;
        std::string per;     // project
        std::string from;    // member
        location per_where;
        std::vector<default_role> defaults;
        bool defined = false;  // made from define role, with the role and member the language declares
    };

    // page  #f7f8fa  dark #0a101c: a color a theme gives one of its roles, light and dark.
    struct theme_color {
        std::string role;
        location where;
        std::string light, dark;
        location light_where, dark_where;
    };

    // define theme harbor "Harbor" { page #f7f8fa dark #0a101c ... }: how a site
    // looks, said as colors for what each is for, light and dark, its fonts, its
    // corners and its depth. Everything else it's drawn with is worked out from these.
    struct theme_declaration {
        std::string name;
        location where;
        std::string title;
        std::string from;  // a theme this one starts from, changing what it says
        location from_where;
        std::vector<theme_color> colors;
        std::optional<std::string> text, heading, code, document;  // fonts
        // heading "Fraunces" from "fonts/Fraunces.ttf": a font the app brings with it, by
        // its family, from a file beside the one declaring the theme.
        std::map<std::string, std::string> font_files;
        location font_files_where;
        std::optional<theme_color> ground;              // the code editor's background
        std::optional<int> corners;
        location corners_where;
        std::optional<std::string> depth;               // flat or raised
        location depth_where;
    };

    // define service github "GitHub" in project { mention::create }: a system that
    // runs commands, not a person, and the commands it may run, one a line. A webhook
    // runs as one. No person may run a command only services allow.
    struct define_service_declaration {
        std::string name;
        location where;
        std::string title;
        std::string in;  // project, when each project connects its own; empty for the app's
        location in_where;
        std::vector<qualified_name> permissions;
    };

    // define role maintainer "Maintainer" in project { project::update ... }: a role
    // each project starts with, and the commands it allows, one a line. The language
    // keeps a project's roles as role records, and who holds them as member records.
    struct define_role_declaration {
        std::string name;
        location where;
        std::string title;
        std::string in;  // project
        location in_where;
        bool services = false;  // for services: service accounts may hold it too
        location services_where;
        std::vector<qualified_name> permissions;
        std::vector<role_holding> holds;
        // keep at least 1 owner: a place never left with fewer of this role.
        int keep = 0;
        std::string keep_role;
        location keep_where;
    };

    // extend role organizations::owner { account::subscribe }: commands a project adds
    // to a role another declares, like a hosted edition's billing for the owners of the
    // open-source app's organizations. Lowering adds them to that role.
    struct extend_role_declaration {
        qualified_name role;
        std::vector<qualified_name> permissions;
    };

    // proofing worked by proofreader, in a workflow whose stages are an enum's
    // choices: the roles that work that stage.
    struct workflow_stage {
        std::string name;  // proofing
        location where;
        std::vector<std::string> roles;  // proofreader
        std::vector<location> role_where;
    };

    // proofing -> press, or press -> proofing by press_operator: a step between two
    // stages written in the code, taken by whoever works the first unless it says.
    struct workflow_step {
        std::string from;  // proofing
        location from_where;
        std::string to;  // press
        location to_where;
        std::vector<std::string> by;  // press_operator, or none: whoever works from
        std::vector<location> by_where;
    };

    // define workflow issue by stage { worked by worked_by  steps step }: the rules an
    // entity moves by. Its stages are the records its field points at, each listing the
    // roles that work it; its steps say which roles move it from one to another. A
    // worker claims one for a while, its lease, and moves it on.
    //
    // When the field is an enum, its stages are the enum's choices, and the workflow
    // says who works each and the steps between them, in the code:
    //
    //     define workflow order by stage {
    //         proofing  worked by proofreader
    //         proofing -> press
    //     }
    // shipped notify chime: how someone is told when one arrives in a stage. Only the
    // chime is made; email and text are words kept for later.
    struct workflow_notice {
        std::string stage;
        location where;
        std::vector<std::string> ways;
        std::vector<location> way_where;
    };

    // on order in printing  run "press {order.id}"  then printed  else proofing: when
    // one arrives in the stage, the agent takes it, runs the program, and moves it on.
    struct agent_rule {
        std::string entity;
        location where;
        std::string stage;
        location stage_where;
        std::string program;  // with {field} parts filled in from what it works
        location program_where;
        std::string then;
        location then_where;
        std::string otherwise;  // else: where a failure goes; none releases it instead
        location otherwise_where;
    };

    // define agent press for press_operator { on … }: a program that works the stages
    // a role for services works, by itself.
    struct define_agent_declaration {
        std::string name;
        location where;
        std::string role;
        location role_where;
        std::vector<agent_rule> rules;
    };

    struct define_workflow_declaration {
        std::string entity;  // issue
        location where;
        std::string by;  // stage: the field it moves along
        location by_where;
        std::string worked_by;  // worked_by: the stage's field listing the roles that work it
        location worked_by_where;
        std::string steps;  // step: the entity of moves, with from, to and roles
        location steps_where;
        int lease_minutes = 10;  // how long a claim lasts unless it's renewed
        location lease_where;
        std::vector<workflow_stage> stages;  // proofing worked by proofreader
        std::vector<workflow_step> moves;    // proofing -> press
        std::vector<workflow_notice> notices;  // shipped notify chime
        std::string done_at;  // shipped: the stage where its work is finished
        location done_at_where;
        // done when stage.status == status::success: for stages kept as records, a field of
        // the record, or of what it points at, and the value saying its work is finished.
        std::vector<std::string> done_when;  // like stage, status
        std::string done_when_value;         // like success
        location done_when_where;
        std::string depends_on;  // packages: a list of what must be done before it leaves its first stage
        location depends_on_where;
        // while closed == none: a record is worked, in its queue and its claim, only while
        // this holds; kept as written, since the workflow is said as code.
        std::string worked_while;
        location worked_while_where;
    };

    struct function_declaration {
        std::string name;
        std::vector<std::string> parameters;
        std::vector<statement> body;
    };

    struct screen_item;

    // A key that runs what's on its line, like ctrl+z or f11: its keys, joined by +.
    struct key_chord {
        std::string chord;
        location where;
    };

    struct content_block {
        // A menu holds links, with the rest of the screen beside it; a region is one
        // part of the screen's layout, like main or side, named in title.
        // A heading's items sit on the screen's title row, at its end. A group is a
        // named part of a workspace's toolbar, with a line between it and the next.
        enum class kind { hero, section, menu, region, heading, group, options };
        kind type = kind::section;
        std::string title;
        std::optional<std::string> anchor;
        std::string hue;  // section "Wiki" color violet: one of the library's hues, or none
        location hue_where;
        std::optional<std::string> icon;  // section "Data" icon database: drawn beside its title
        location icon_where;
        bool steps = false;  // section "How we work" as steps: its sections are done in order
        location steps_where;
        // options "Page" icon page { ... }: a tool that opens its lines, each a choice
        // drawn with what it's for, over everything until one's chosen.
        bool small = false;   // group "Rotate" small: its buttons are icons, their words said when pointed at
        int across = 0;       // group "Align" small rows of 3: this many in each row, rather than two rows, the first the longer
        location across_where;
        bool at_end = false;  // group "View" at end: at the toolbar's far end
        std::vector<screen_item> items;
    };

    struct content_text {
        // An overline is the short line over a hero's or a section's title; a statement,
        // a front page's one sentence that stands for the site.
        enum class kind { text, code, markdown, overline, statement };
        kind type = kind::text;
        std::string value;
        expression_ptr when;  // text "Implemented by {issue_page.implementer}" when issue_page.implementer != none
        bool subtitle = false;  // subtitle "{project_page.summary}": under the screen's title, as running words
        bool document = false;  // markdown "license/*.md" as document: set as a printed document, in the theme's document font
    };

    struct content_link {
        std::string label;
        std::string target;                            // a route inside the link's namespace, #anchor, or https:// address
        std::optional<qualified_name> namespace_name;  // link namespace projects "See the projects"
        std::optional<std::string> icon;               // icon workflow: drawn as that, its words still naming it
        location icon_where;
        std::string built;                             // link build.release: what the site was built from, release or source
        location built_where;
        std::vector<key_chord> keys;                   // key ctrl+o: opened by that key too
    };

    struct table_column {
        location where;
        expression_ptr value;
        std::optional<std::string> label;
        // delete "Remove" when person != me: a row's button shown only on the rows
        // where it holds, read from the row's own fields, and its text as written.
        expression_ptr when;
        std::string when_written;
        std::optional<std::string> icon;  // issue::create "New issue" icon add: a toolbar's button drawn as that
        location icon_where;
    };

    // track milestone over board_page.milestones { due }: a list's records as cards in
    // a row, in its order, joined by arrows, each its title and what the block shows;
    // picking one keeps the rows whose field is it, like the issues in a milestone.
    struct track_setting {
        std::string field;  // the rows' field naming one of the records, like milestone
        location where;
        qualified_name over;
        std::string over_list;
        std::vector<table_column> columns;  // what a card shows besides its title, like due
        bool hide_empty = false;            // hide when empty: a record no row names isn't on it
        bool picked_last = false;           // picked last: the last record on it is picked until another is
    };

    struct table_item {
        qualified_name view;
        std::optional<std::string> list;  // table issue_page.comments: one of the view's lists
        std::vector<table_column> columns;
        std::optional<std::string> link;  // the screen each row opens, like /books/:book
        location link_where;
        std::optional<std::string> by;    // table project_page.issues by status: a tab for each of its choices
        location by_where;
        // by stage over project_page.stages: a tab for each of a list's records, in its order.
        std::optional<qualified_name> by_over;
        std::string by_over_list;
        std::vector<std::string> search;  // search title labels: a box that finds rows by these
        location search_where;
        std::optional<std::string> sort;  // sort -number: rows in this order, - for largest or latest first
        bool sort_descending = false;
        location sort_where;
        std::optional<int> page;          // page 25: this many rows at a time
        location page_where;
        std::optional<std::string> reorder;  // reorder position: rows put in order by dragging, which sets this
        bool reorder_descending = false;     // reorder depth descending: the highest first
        bool hide_empty = false;             // hide when empty: not there at all while it has no rows
        std::optional<std::string> empty;    // empty "Add a picture to start": what it says while it has no rows
        location reorder_where;
        // picture picture fit: a small picture of a file each row holds, before its cells.
        std::optional<std::string> picture;
        std::string picture_sits = "fill";
        location picture_where;
        // download "audit.csv": a button that saves every row it keeps, in a file of
        // that name, as comma-separated values.
        std::optional<std::string> download;
        location download_where;
        // only due <= 2 weeks from now: the rows it keeps, worked out when it's shown,
        // like a card's filter.
        expression_ptr only;
        location only_where;
        // tint by priority: each row colored by how urgent its choice is, the first of
        // the field's choices the most, as Trac colored a ticket's priority.
        std::string hue;  // color violet: its rows' links in one of the library's hues
        location hue_where;
        std::optional<std::string> tint;
        location tint_where;
        std::optional<track_setting> track;
    };

    // grid project_page.steps by from and to over project_page.stages { roles.title }:
    // what goes between two of a list's things, a row and a column for each, and in a
    // cell what it holds, like the roles that may take a move.
    struct grid_item {
        qualified_name view;
        std::optional<std::string> list;
        std::string from, to;  // the fields naming an entry's row and column
        location from_where, to_where;
        qualified_name over;
        std::string over_list;
        expression_ptr cell;   // what a cell shows, or none for a mark
        // diagram ...: the same drawn, each thing a box and each entry an arrow.
        bool drawn = false;
    };

    struct form_field {
        location where;
        std::string name;
        expression_ptr value;             // = suggest_code(start_date)
        std::optional<std::string> hint;
        std::optional<std::string> label; // body "Comment": what the field is called, rather than its name
        // How it's asked for, once the project is checked: text, email, date, number,
        // slider, markdown, boolean, list, choice, radio, pick, choices, file or files.
        mutable std::string kind;
    };

    struct form_item {
        std::vector<qualified_name> commands;
        std::vector<form_field> fields;
        std::optional<std::string> submit;  // form project::update "Save changes": what its button says
    };

    struct confirm_item {
        qualified_name command;
        std::string message;
    };

    // issue::close "Close issue" when issue_page.status == status::open: a command's
    // button, named for what it does, and shown only while the page says it applies.
    struct button_item {
        qualified_name command;
        std::optional<std::string> label;
        expression_ptr when;
        // issue::move along project_page.steps: a button for each step from where the
        // entity is now that the person's roles may take, moving it to the step's to.
        std::optional<qualified_name> along;  // the view, like project_page
        std::string along_list;               // its list of steps, like steps
        std::optional<std::string> icon;      // icon edit: drawn as that, its words still naming it
        location icon_where;
        std::vector<key_chord> keys;          // key ctrl+s: run by that key too, as long as it's shown
        // layer::scale "2×" { by = 2 }: what it's sent besides what it acts on, worked
        // out as it's pressed, from the page's views and what's picked.
        std::vector<field_value> given;
        expression_ptr pressed_when;          // on when layer.weight >= 700: drawn pressed while it holds
        std::optional<std::string> then_open;  // then open /:poster: where what it made is opened
        location then_where;
        std::optional<std::string> about;  // about "For slides and video": what it's for, said beside it among options
    };

    // collage poster_page.layers size width height { ... }: a picture made of layers, as
    // big as two of the view's values say, in its own pixels (decision 0019). Its rows
    // are its layers, lowest first, each drawn from the parts it has: a place, a
    // picture, its crop and turns, words and how they look; the first, when it's a
    // picture, is its background, sitting as the view's background part says. Each part
    // is kept in the field named like it, unless the collage says another, as in
    // words caption, and the runtimes know only the parts.
    struct collage_item {
        qualified_name view;
        std::string list;
        location list_where;
        std::string width = "width", height = "height";
        location size_where;
        std::optional<qualified_name> place;  // place layer::place: a layer moved or resized, kept
        std::optional<qualified_name> reshape;  // reshape layer::reshape: a path's nodes moved, its points and box kept
        std::optional<qualified_name> group;  // group layer::group: what puts a layer in a group, or takes it out
        std::optional<qualified_name> add;    // add layer::create: what makes new layers, like words added
        std::optional<qualified_name> crop;   // crop layer::crop: a picture cropped in place, kept
        std::optional<qualified_name> write;  // write layer::write: words written in place, or resized, kept
        std::optional<qualified_name> remove;  // delete layer::delete: words written away to nothing, gone
        // paste: files dropped or pasted made pictures by its add.
        bool paste = false;
        location paste_where;
        // words caption, stacked by order, ...: the fields playing parts other than those
        // named like them, by part, and where each was said.
        std::map<std::string, std::string> fields;
        std::map<std::string, location> fields_where;
        // keep preview poster::preview into preview 480 wide: a small picture of it, drawn
        // once changes settle and sent to a command on what the view is per, never a
        // step undo takes back.
        std::optional<qualified_name> preview;
        std::string preview_into;
        int preview_wide = 480;
        location preview_where;
        bool snap = false;                    // snap to edges: to the collage's and every other layer's
        bool clear = false;                   // clear: nothing behind its layers, shown over a checkerboard, saved clear
        // in drawing_page.layers by layer: its rows are objects held in layers, the rows
        // of another list of the same view, each object in the one its field names. The
        // layers are drawn in their depth's order, each object within its own, and a
        // hidden layer's objects aren't drawn. A layer's name is its name, and a new one
        // is made by its entity's create.
        std::optional<std::string> in_list;
        std::string in_field;
        location in_where;
        expression_ptr match_colors;          // match colors when poster_page.matched: its pictures' colors moved toward one another's
        location snap_where;
        std::optional<std::string> empty;     // empty "Drop a picture": said while it has no layers
        // draw layer::create { shape = shape::ellipse } when tool == tool::ellipse: a drag
        // on it makes a layer as big as the drag, given these values, while its when
        // holds; the first whose when holds draws. By line, it's a line from where the
        // drag began to where it ended, kept as a path's points. The choices its when
        // reads go back to their first once it's drawn.
        struct drawing {
            qualified_name command;
            std::vector<field_value> given;
            std::optional<std::string> by;
            location by_where;
            expression_ptr when;
        };
        std::vector<drawing> draws;
        std::optional<std::string> over;      // over layer { ... }: buttons above the picked layer
        location over_where;
        std::vector<screen_item> tools;
    };

    // layers drawing_page.layers { hide layer::show  order layer::update  delete layer::delete }:
    // the layers of the screen's collage over the same list, topmost first, each an eye
    // that hides or shows it by its hide, its preview in the collage's proportions, the
    // layer alone drawn where it is, its name, up and down by its order, which renumbers
    // their depth, and delete. A press on one picks it, as the collage does.
    struct layers_item {
        qualified_name view;
        std::string list;
        location list_where;
        std::optional<qualified_name> hide, order, remove;
        // rename layer::rename: a name double-clicked is written in place, a layer's, or,
        // by a command on the collage's objects, an object's.
        std::vector<qualified_name> renames;
    };

    // paper document_page.blocks { ... }: a list's rows as one upright sheet of paper,
    // written on in place (decision 0022). Its rows are its blocks, in the order their
    // position says, each set as its kind says and aligned as its align says. Each part
    // is kept in the field named like it, unless a line names another, as in words body.
    struct paper_item {
        qualified_name view;
        std::string list;
        location list_where;
        std::optional<qualified_name> write;   // write block::write: a block's words written, kept
        std::optional<qualified_name> add;     // add block::create: Enter splitting a block, or the first words on an empty page
        std::optional<qualified_name> remove;  // delete block::delete: a block joined into the one before
        std::map<std::string, std::string> fields;
        std::map<std::string, location> fields_where;
        std::optional<std::string> empty;      // empty "Title": where an empty page's first words go
        bool outline = false;                  // outline: its title and headings beside it, each going there
        expression_ptr outline_when;           // outline when contents: and only while it holds, as a drawer
        bool paste = false;                    // paste: pictures pasted or dropped, made picture blocks by its add
        bool progress = false;                 // progress: a bar across its top, as far along as it's been read
        location paste_where;
        expression_ptr warm;                   // warm when coffee: set on warm paper, as under a reading lamp, while it holds
        expression_ptr spread;                 // side by side when spread: its pages two at a time, while it holds
        expression_ptr read_only;              // read only when reading: read, not written on, while it holds
    };

    // insert "Equation" icon equation { kind = kind::math }: a block made after the one
    // the screen's paper has the caret in, given these values, the caret then in it.
    struct paper_insert {
        // larger "Larger" or smaller "Smaller": the reader's page made larger or smaller, as
        // zooming a book does, rather than a block inserted.
        int sizes = 0;
        std::optional<std::string> label;
        std::optional<std::string> icon;
        location icon_where;
        std::vector<key_chord> keys;
        std::vector<field_value> given;
    };

    // crop "Crop" icon crop key c, edit words "Edit", add words "Text": what the screen's
    // collage does to its picked layer, or adds to it, from a toolbar or over the layer.
    struct collage_button {
        enum class kind { crop, edit_words, add_words, add_pictures, paste, screenshot, copy, save, ungroup };
        kind does = kind::crop;
        // copy layer as picture, save poster_page as picture: the picked layer, or all of it.
        std::optional<std::string> of;
        location of_where;
        bool svg = false;  // as svg: from its vectors, an SVG, rather than a picture of it
        std::optional<std::string> label;
        std::optional<std::string> icon;
        location icon_where;
        std::vector<key_chord> keys;
    };

    // on open open /posters/:poster for posters where poster.first_layer == none, or on open
    // poster::create { ... } then open /posters/:poster: what a screen does each time it's
    // opened, the first of its lines that applies (decision 0020). One opens the first
    // row of a view's list the condition holds for, the row named by what it's of; the
    // other runs a command, as its button would.
    // picture poster.preview fit: the file a view's value, or a field of what's picked,
    // holds, as wide as its place, all of it fitted in or filling it.
    struct picture_item {
        expression_ptr source;
        bool fit = false;
    };

    struct on_open_item {
        std::optional<std::string> route;
        location route_where;
        qualified_name view;
        std::optional<std::string> list;
        expression_ptr where;
        std::vector<screen_item> run;  // the command line it runs, when it doesn't open a row
    };

    // What a collage button asks of its collage, as the runtimes name it, and the words
    // it's drawn with when it says none. Copied or saved, it's the picked layer when it
    // names the collage's pick, over.
    inline std::string asked_of(const collage_button& b, const std::optional<std::string>& over) {
        using kind = collage_button::kind;
        bool picked = b.of && over && *b.of == *over;
        switch (b.does) {
            case kind::crop: return "crop";
            case kind::edit_words: return "edit";
            case kind::add_words: return "add";
            case kind::add_pictures: return "add pictures";
            case kind::paste: return "paste";
            case kind::screenshot: return "screenshot";
            case kind::copy: return std::string(b.svg ? "copy svg" : "copy") + (picked ? " picked" : "");
            case kind::save: return std::string(b.svg ? "save svg" : "save") + (picked ? " picked" : "");
            case kind::ungroup: return "ungroup";
        }
        return "";
    }
    inline std::string label_of(const collage_button& b) {
        using kind = collage_button::kind;
        if (b.label) return *b.label;
        switch (b.does) {
            case kind::crop: return "Crop";
            case kind::edit_words: return "Edit";
            case kind::add_words: return "Text";
            case kind::add_pictures: return "Pictures";
            case kind::paste: return "Paste";
            case kind::screenshot: return "Screenshot";
            case kind::copy: return "Copy";
            case kind::save: return "Save";
            case kind::ungroup: return "Ungroup";
        }
        return "";
    }

    // The plain names a condition reads, once each, like tool in tool == tool::pen:
    // of them, the choices a collage's draw sets back to their first once it's drawn.
    inline void names_read(const expression& e, std::vector<std::string>& out) {
        if (auto* name = std::get_if<name_expression>(&e.node); name && name->name.parts.size() == 1) {
            if (std::ranges::find(out, name->name.text()) == out.end()) out.push_back(name->name.text());
        } else if (auto* unary = std::get_if<unary_expression>(&e.node); unary && unary->operand) {
            names_read(*unary->operand, out);
        } else if (auto* binary = std::get_if<binary_expression>(&e.node)) {
            if (binary->left) names_read(*binary->left, out);
            if (binary->right) names_read(*binary->right, out);
        }
    }

    // pick layer from poster_page.layers: a row of a view's list the screen holds, picked
    // with a click on a table or a collage of it, read by its name, layer.size, and
    // what a command on its entity acts on (decision 0017).
    struct pick_item {
        std::string name;
        location name_where;
        qualified_name view;
        std::string list;
        location list_where;
    };

    // toggle layers "Layers" icon layers: something on or off, like a side panel, that
    // the screen reads by its name, side when layers, remembered for the person on
    // that device and never stored or sent (decision 0017).
    struct toggle_item {
        std::string name;
        location name_where;
        // toggle tool::pen: one of an enum's choices, pressed while it's the one; the
        // screen's toggles over one enum are one choice, its first until another's pressed.
        std::optional<std::string> choice;
        std::optional<std::string> label;
        std::optional<std::string> icon;
        location icon_where;
        std::vector<key_chord> keys;
    };

    // undo "Undo" icon undo key ctrl+z, or redo: the page's last step taken back, or
    // taken again, and said in the screen's messages (decision 0018).
    struct history_item {
        bool redo = false;
        std::optional<std::string> label;
        std::optional<std::string> icon;
        location icon_where;
        std::vector<key_chord> keys;
    };

    // messages: where what was just done is said, like "Undid place", or why a
    // command was turned down, until the next.
    struct messages_item {};

    // full screen "Full screen" icon fullscreen key f11: the window filling the
    // screen, and back.
    struct full_screen_item {
        std::optional<std::string> label;
        std::optional<std::string> icon;
        location icon_where;
        std::vector<key_chord> keys;
    };

    // board project_page.issues by stage over project_page.stages { ... }: a list's
    // rows as cards in columns, a column for each of the over list's things, in its
    // order; with move issue::move along project_page.steps, a card is dragged to
    // another column along a step the person's roles may take.
    struct board_item {
        qualified_name view;
        std::optional<std::string> list;
        std::string by;  // the field naming a card's column, like stage
        location by_where;
        qualified_name over;
        std::string over_list;
        std::optional<std::string> link;  // the screen a card opens, like /:project/issues/:issue
        location link_where;
        std::optional<button_item> move;
        std::vector<table_column> columns;  // what a card shows, its title first
        std::vector<std::string> search;    // search title labels: a box that finds cards by these
        std::optional<std::string> tint;    // tint by priority: each card colored as a table's row
        location tint_where;
        std::optional<track_setting> track;
        std::optional<std::string> reorder;  // reorder position: cards dragged into order in their column
        location reorder_where;
        // chime when notify has chime: a column whose row's list holds the word chimes
        // when a card arrives; status by status: its row's field is its cards' dot.
        std::string chime_field, chime_word;
        location chime_where;
        std::string status_field;
        location status_where;
    };

    // calendar planner.tasks by due { month when month  move task::plan  tint by priority
    // title }: a list's rows on the days a date of theirs says, a week of days side by
    // side, or a month's grid while month's condition holds; one dragged to another
    // day is sent to the move command with its date there, its time of day kept
    // (decision 0023).
    struct calendar_item {
        qualified_name view;
        std::string list;  // or none for the view's own, its rows
        std::string by;  // the date placing a row on its day, like due
        location by_where;
        expression_ptr month;  // month when month: a month's grid while it holds
        std::optional<qualified_name> move;
        location move_where;
        std::optional<std::string> tint;
        location tint_where;
        std::vector<table_column> columns;  // what a row shows, its title first
    };

    // canvas board_page.issues by stage over board_page.stages link /:project/:issue { ... }:
    // a list's rows as cards placed anywhere on a surface that pans and zooms, each
    // where its fields say, dragged and resized, and placed by a command once let go.
    // One not placed yet is laid out in columns, by the field named after by, like a
    // board's, so a board's cards shown on a canvas start where a board would show them.
    struct canvas_item {
        qualified_name view;
        std::optional<std::string> list;
        std::string by;  // optional: the field laying out the cards not placed yet, like stage
        location by_where;
        qualified_name over;
        std::string over_list;
        std::optional<std::string> link;
        location link_where;
        std::optional<qualified_name> place;  // place issue::place: the command a card's x y w h are sent to
        location place_where;
        std::string x = "x", y = "y", w = "w", h = "h";  // at x y  size w h: the fields it's placed by
        location at_where, size_where;
        std::string kind;  // kind kind: the field saying how a card's drawn, like note or text
        location kind_where;
        double grid = 0, origin_x = 0, origin_y = 0;  // grid 160 from 40 80: where a card snaps when let go
        location grid_where;
        // regions by width: a band down the canvas for each of over's rows, as wide as
        // its field says, and a card let go in another band moves there, by move; the
        // line between two is dragged to make one wider, saved by resize.
        std::string regions;
        location regions_where;
        std::optional<button_item> move;
        std::optional<qualified_name> resize;
        location resize_where;
        // paste issue::create into attachments: a file pasted or dropped on the canvas
        // makes a card by this command, the file in that field, where it landed.
        std::optional<qualified_name> paste;
        std::string paste_into;
        location paste_where;
        std::string picture;  // picture attachments: the field whose first image a card shows
        location picture_where;
        std::vector<table_column> columns;  // what a card shows, its title first
    };

    // pipeline board_page.issues by stage over board_page.stages { lanes by labels
    // path by kinds  title }: a list's rows grouped in a lane for each kind a card says
    // it is, each a row of the stages its kind goes through, in order: those before
    // its own done, its own in its status's color, and the one it failed in, when it
    // says so in its data's at, red. A stage goes on every kind's path when it names
    // no kind, unless it's where cards fail.
    struct pipeline_item {
        qualified_name view;
        std::optional<std::string> list;
        std::string by;  // the field naming a card's stage, like stage
        location by_where;
        qualified_name over;
        std::string over_list;
        std::optional<std::string> link;
        location link_where;
        std::string lanes;  // lanes by labels: the card's field saying what it is
        location lanes_where;
        std::string path;  // path by kinds: the stage's field listing the kinds that go through it
        location path_where;
        std::string status_field;  // status by status: the stage's status, its cards' dot
        location status_where;
        std::vector<table_column> columns;  // what a row shows, its title first
    };

    // cards project_page.boards link /:project/boards/:board { ... }: a list's rows as
    // large cards, each with what its block shows, its title first; a tally of what
    // it holds, like its issues by stage; and filters, links that open it filtered.
    struct cards_filter {
        std::string label;
        location where;
        expression_ptr condition;  // author == me
        std::string hue;           // color red: one of the library's hues, or none
        location hue_where;
    };

    struct cards_item {
        qualified_name view;
        std::optional<std::string> list;
        std::optional<std::string> link;
        location link_where;
        std::vector<table_column> columns;
        // tally project_page.issues by board and stage
        std::optional<qualified_name> tally;
        std::string tally_list, tally_by, tally_and;
        location tally_where;
        std::vector<cards_filter> filters;
        bool hide_empty = false;  // hide when empty: not there while it has no cards, or can't be read
        // picture preview fit: a file field's picture on top of each card, all of it
        // fitted in, or filling it.
        std::optional<std::string> picture;
        std::string picture_sits = "fill";
        location picture_where;
    };

    // chart agent_page.stats.cpu "CPU" percent: a run of numbers, newest last, drawn
    // as a line over time with its latest value, as btop draws one. Its numbers are a
    // field of the view, or a name in a field of data, like stats.cpu. They're shown
    // as they are, or as percent (out of 100), bytes, or a rate of bytes a second.
    struct chart_series {
        std::vector<std::string> path;  // stats, cpu: a field, then a name within it
        std::string title;
        std::string unit;  // "", percent, bytes or rate
        location where;
        std::string text() const {
            std::string out;
            for (const auto& p : path) out += (out.empty() ? "" : ".") + p;
            return out;
        }
    };

    // charts agent_page { stats.cpu "CPU" percent ... }: several, side by side.
    struct chart_item {
        qualified_name view;
        std::vector<chart_series> series;
    };

    // monitor agents_page.agents link /:project/agents/:agent { ... }: a tile for each
    // record whose status changes from one thing to another, like an agent or a
    // machine, rather than moving along stages: its title, its status as a dot and a
    // word, what it's doing and how far, and small charts of what it reports. One not
    // heard from for a while shows as offline.
    struct monitor_item {
        qualified_name view;
        std::string list;
        std::optional<std::string> link;
        location link_where;
        std::string title = "title";  // what names a tile
        location title_where;
        std::string status;  // the field saying how it is, like working or idle
        location status_where;
        std::string doing;  // what it's doing now, a line
        location doing_where;
        std::string step, steps;  // progress step of steps: how far, within what it's doing
        location progress_where;
        std::string since;  // when its status began, for how long it's been so
        location since_where;
        std::string seen;  // seen seen_at 15: when it was last heard from, and after how many seconds it's offline
        int seen_seconds = 0;
        location seen_where;
        std::vector<chart_series> charts;
    };

    // component workbench: a hand-written component, beside the .one file, drawn where
    // the item is: components/workbench.tsx, a React component, on the web, and
    // components/workbench.cpp, a function of the page giving a view, in the desktop
    // and terminal apps.
    struct component_item {
        std::string name;
    };

    // The file a component is written in, beside the .one file that draws it: .tsx for
    // the web, .cpp for the desktop and the terminal.
    inline std::string component_file(const std::string& one_file, const std::string& name, const std::string& extension = ".tsx") {
        auto folder = std::filesystem::path(one_file).parent_path();
        return (folder / "components" / (name + extension)).string();
    }

    // thread issue_page.comments: a list of what people wrote, each with who wrote it
    // and when. timeline issue_page.history: an entity's changes, each said as what
    // happened, like "Ada closed this". Both read one of a view's lists.
    struct thread_item {
        qualified_name view;
        std::string list;
    };

    struct timeline_item {
        qualified_name view;
        std::string list;
        std::optional<std::string> link;  // timeline project_page.timeline link /:project/issues/:issue: what each change was to
        location link_where;
        std::optional<std::string> title;  // timeline news.changes "What's new": said above it, shown while it holds something
        // new when created_at > news.seen: the changes the condition holds for are
        // marked new, reading other views as they were when the page opened, and on
        // open dispatch reader::create runs once it opens with something new in it.
        expression_ptr fresh;
        location fresh_where;
        std::optional<qualified_name> opened;
        location opened_where;
    };

    // copy issue_page "Copy issue": a button that copies everything a view holds, as
    // Markdown, to be pasted somewhere else whole.
    struct copy_item {
        qualified_name view;
        std::optional<std::string> label;
    };

    // themes: themes made at random from a palette, tried on the whole app at once,
    // each written out as a project says it, to copy.
    struct themes_item {};

    // details issue_page { status "Status"  implementer "Implemented by" }: a view's
    // values, each beside what it is, the ones with nothing in them left out.
    struct details_item {
        qualified_name view;
        std::vector<table_column> fields;
        std::optional<std::string> tint;  // tint by priority: the box colored as a table's row
        location tint_where;
    };

    // find "Search this project" { project_page.issues "Issues" link /:project/:issue by
    // number title labels }: a box that finds rows of several lists as it's typed in,
    // each list's under its own label, each row a link.
    struct find_source {
        location where;
        qualified_name view;
        std::string list;
        std::string label;
        std::optional<std::string> link;
        location link_where;
        std::vector<std::string> by;  // the fields it's found by, the first shown, a number before it
    };

    struct find_item {
        std::string label;
        std::vector<find_source> sources;
    };

    struct screen_item {
        location where;
        std::variant<content_block, content_text, content_link, table_item, form_item,
                     confirm_item, button_item, component_item, thread_item, timeline_item, copy_item, themes_item, details_item, grid_item, board_item, cards_item, chart_item, monitor_item,
                     find_item, canvas_item, pipeline_item, toggle_item, history_item, messages_item, full_screen_item, pick_item, collage_item, layers_item, collage_button, on_open_item, picture_item, paper_item, paper_insert, calendar_item>
            node;
        // board … when board_page.style == style::kanban: shown only while it holds, so
        // one screen shows a list as a board or a canvas, as a field says.
        expression_ptr when = nullptr;
    };

    struct screen_declaration {
        std::string title;           // empty for an untitled screen, screen /:project/settings, which shows the site's
        bool title_is_name = false;  // screen docs /:page, rather than screen "Shelf" /shelf
        std::string route;
        // screen /a /b { … }: its other addresses, each read as a screen of its own with
        // the same items, from the block's first token.
        std::vector<std::string> more_routes;
        std::size_t block_at = 0;
        std::optional<std::string> layout;  // layout two_columns: how its regions are laid out
        location layout_where;
        std::optional<std::string> landing;  // landing grid: a front page drawn in one of uione's designs
        location landing_where;
        // under /:project/boards/:board "{issue_page.board_title}": the page above it,
        // though its address doesn't say so, and what that page is called from here.
        std::optional<std::string> under;
        std::string under_title;
        location under_where;
        std::vector<screen_item> items;
    };

    struct picker_declaration {
        std::string entity;
        std::string view;
    };

    struct webhook_handler {
        std::string event;  // commit or pull_request
        location where;
        std::vector<statement> body;
    };

    // webhook github /hooks/github { for project by repository  on commit { ... } }
    // webhook stripe /hooks/stripe { for organization  on subscription { ... } }
    struct webhook_declaration {
        std::string provider;
        location provider_where;
        std::string route;
        std::string scope;       // project
        std::string repository;  // the field naming its repository, for github
        location scope_where;
        std::vector<webhook_handler> handlers;
        std::string as;          // the service it runs as, which allows what it dispatches
        location as_where;
    };

    // backend deploy: Go written by hand, backend/deploy.go beside the .one file, built
    // into the namespace's package with the generated code.
    struct backend_declaration {
        std::string name;
    };

    // each project { start = stage::triage }: a command's body done to every stored
    // entity of a kind, or to those its where names, like each role where name ==
    // "developer".
    struct once_step {
        std::string entity;
        location entity_where;
        expression_ptr where;  // name == "developer", or none for all of them
        std::vector<statement> body;
        bool remove = false;  // delete each invitation where email == me.email
    };

    // once "2026-10-07 workflows" { each project { ... } }: a change to what's stored
    // that a deploy brings, done the first time the backend starts with it and never
    // again, its steps in order.
    struct once_declaration {
        std::string name;
        std::vector<once_step> steps;
        // on signin { ... }: done each time someone opens the app signed in, as them,
        // with me.email the email their sign-in vouches for, rather than once.
        bool signin = false;
    };

    // The file a backend is written in, beside the .one file that names it.
    inline std::string backend_file(const std::string& one_file, const std::string& name) {
        auto folder = std::filesystem::path(one_file).parent_path();
        return (folder / "backend" / (name + ".go")).string();
    }

    struct declaration;

    struct namespace_declaration {
        std::string name;
        std::vector<declaration> declarations;
        std::optional<std::string> at;  // namespace studio at /: where its screens are, rather than /studio
    };

    // footer { text "© {year} Ada" }: what's at the foot of every page, drawn with a
    // screen's items, in one line (bar) or in columns, each a section.
    struct footer_declaration {
        std::string layout = "bar";
        location layout_where;
        std::vector<screen_item> items;
    };

    // header { badge news.unread }: what's beside the site's name on every page. A
    // badge shows a number a view holds, like what's new to the person reading,
    // when it's more than none.
    struct header_badge {
        location where;
        std::string view;   // news
        std::string value;  // unread
    };

    // header /:project { component project_header }: items above every screen whose
    // address goes on from that path.
    struct header_declaration {
        std::vector<header_badge> badges;
        // navigation projects docs about: the namespaces whose screens the top bar lists
        // first, in this order; the rest follow as their files come.
        std::vector<qualified_name> navigation;
        std::optional<std::string> route;
        location route_where;
        std::vector<screen_item> items;
    };

    // notify planner.reminders at remind_at { title  task::finish "Done" }: whatever
    // screen is open, a row's time come, said once by the system with what it shows
    // and its buttons, each acting on the row (decision 0024).
    struct notify_declaration {
        qualified_name view;
        std::string list;  // or none for the view's own, its rows
        std::string at;    // the date saying when, like remind_at
        location at_where;
        std::vector<table_column> shown;    // what it says, its first line first, like title
        std::vector<button_item> buttons;  // on the notice, each acting on its row
    };

    struct declaration {
        location where;
        std::variant<project_declaration, namespace_declaration, format_declaration,
                     entity_declaration, command_declaration, view_declaration,
                     role_declaration, function_declaration, screen_declaration, picker_declaration,
                     webhook_declaration, backend_declaration, enum_declaration, roles_declaration, once_declaration,
                     import_declaration, settings_declaration, footer_declaration, header_declaration, define_role_declaration,
                     define_service_declaration, theme_declaration, define_workflow_declaration, define_agent_declaration, extend_role_declaration,
                     extend_command_declaration, notify_declaration>
            node;
    };

    struct file {
        std::string path;
        std::vector<declaration> declarations;
        // Where an import goes: before the first declaration, and the comment that's
        // its own, below the comments that open the file, like its license.
        location imports_at{1, 1};
    };

    // The choice a name stands for, when it's one of a field's: written with the
    // field's enum, as it has to be, like visibility::public, or plainly, as public.
    inline std::optional<std::string> choice_of(const qualified_name& name, const field& f) {
        if (f.choices.empty()) return std::nullopt;
        const std::string* value = nullptr;
        if (name.parts.size() == 1) value = &name.parts[0];
        if (name.parts.size() == 2 && (name.parts[0] == f.name || name.parts[0] == f.enum_name)) value = &name.parts[1];
        if (!value || std::find(f.choices.begin(), f.choices.end(), *value) == f.choices.end()) return std::nullopt;
        return *value;
    }

    // unread = count(changes where created_at > seen): a count of a view's own list,
    // of the rows that pass, compared with its other values. The list it counts, and
    // its where, when a value is one.
    struct row_count {
        const view_each* list = nullptr;
        const where_expression* where = nullptr;  // none when it counts every row
    };

    inline row_count counted_rows(const view_declaration& v, const view_value& value) {
        auto* call = std::get_if<call_expression>(&value.value->node);
        auto* callee = call ? std::get_if<name_expression>(&call->callee->node) : nullptr;
        if (!callee || callee->name.text() != "count" || call->arguments.size() != 1) return {};
        const expression& argument = *call->arguments[0];
        auto* w = std::get_if<where_expression>(&argument.node);
        auto* source = std::get_if<name_expression>(&(w ? *w->source : argument).node);
        if (!source || source->name.parts.size() != 1) return {};
        for (const auto& each : v.each) {
            if (each.name && *each.name == source->name.parts[0]) return {&each, w};
        }
        return {};
    }

    // A collage's parts: what each is, the type it's kept as, and whether it's of the
    // view rather than its layers. A layer must have a place; every other part is drawn
    // when it's there.
    struct collage_part {
        const char* name;
        const char* type;  // number, text, file, or the choices it's one of
        bool needed = false;
    };

    inline const std::vector<collage_part>& collage_parts() {
        static const std::vector<collage_part> all{
            {"x", "number", true}, {"y", "number", true}, {"w", "number", true}, {"h", "number", true},
            {"depth", "number"}, {"picture", "file"}, {"picture_width", "number"}, {"picture_height", "number"},
            {"crop_x", "number"}, {"crop_y", "number"}, {"crop_w", "number"}, {"crop_h", "number"},
            {"turns", "number"}, {"mirrored", "number"}, {"around", "clear edges color harmonize"}, {"corners", "number"},
            {"words", "text"}, {"size", "number"}, {"color", "text"}, {"weight", "number"}, {"italic", "number"},
            // A shape's, when it's neither a picture nor words (decision 0025): what it is, a path's
            // points, its fill and stroke, and how much of it shows.
            {"shape", "rectangle ellipse path"}, {"points", "text"}, {"fill", "text"}, {"stroke", "text"}, {"stroke_width", "number"}, {"opacity", "number"},
            // Whether it's hidden: not drawn, picked or saved while it's anything but 0.
            {"hidden", "number"},
            // What it's called in a list of the layers, in place of what it is.
            {"name", "text"},
            // The group it's in, another of them: a group is drawn as what's in it, and
            // picked, moved and resized as one.
            {"group", "self"},
        };
        return all;
    }

    // A paper's: a block's words and its place in the order, and how it's set.
    inline const std::vector<collage_part>& paper_parts() {
        static const std::vector<collage_part> all{
            {"words", "text", true}, {"position", "number", true}, {"kind", "title subtitle heading text quote math code picture"}, {"align", "left center right"},
            // A picture's: the file, its own size, how much of the page's width it runs, and how round its corners are.
            {"picture", "file"}, {"picture_width", "number"}, {"picture_height", "number"}, {"width", "number"}, {"corners", "number"},
        };
        return all;
    }

    // The field each part is kept in: the one the element names, or else the one named
    // like the part, when its rows hold it.
    inline std::map<std::string, std::string> parts_fields(const std::map<std::string, std::string>& said, const view_each* rows, const std::vector<collage_part>& parts) {
        auto holds = [](const std::vector<view_value>& values, const std::string& name) {
            return std::ranges::any_of(values, [&](const view_value& v) {
                if (v.name) return *v.name == name;
                auto* plain = v.value ? std::get_if<name_expression>(&v.value->node) : nullptr;
                return plain && plain->name.text() == name;
            });
        };
        std::map<std::string, std::string> out;
        for (const auto& part : parts) {
            if (auto named = said.find(part.name); named != said.end()) out[part.name] = named->second;
            else if (rows && holds(rows->rows, part.name)) out[part.name] = part.name;
        }
        return out;
    }

    inline std::map<std::string, std::string> collage_fields(const collage_item& c, const view_each* layers) { return parts_fields(c.fields, layers, collage_parts()); }
    // And the one part of the page as a whole: its face, the type it's all set in, a value
    // of its view, kept on what the view is per.
    inline const collage_part& paper_face() {
        static const collage_part face{"face", "book modern lyric typewriter"};
        return face;
    }

    inline std::map<std::string, std::string> paper_fields(const paper_item& p, const view_each* blocks, const std::vector<view_value>* values = nullptr) {
        auto out = parts_fields(p.fields, blocks, paper_parts());
        if (auto said = p.fields.find("face"); said != p.fields.end()) out["face"] = said->second;
        else if (values && std::ranges::any_of(*values, [](const view_value& v) {
                     if (v.name) return *v.name == "face";
                     auto* plain = v.value ? std::get_if<name_expression>(&v.value->node) : nullptr;
                     return plain && plain->name.text() == "face";
                 }))
            out["face"] = "face";
        return out;
    }

} // namespace one::language
