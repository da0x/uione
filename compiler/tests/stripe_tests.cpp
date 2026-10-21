// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Stripe: a webhook that tells the app about its customers' subscriptions and
// invoices, which it keeps with its own commands, and calls a command makes for the
// customer it's on: checkout, the portal, a change, and cancel.

#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "generators/api.hpp"
#include "generators/infrastructure.hpp"
#include "language/checker.hpp"
#include "language/parser.hpp"

using namespace one;

namespace {

    const std::string root = UIONE_ROOT;

    const std::string shop = R"(import one

project shop {
	domain    "shop.example"
	firebase  "shop-example"
	region    "us-east4"
}

namespace billing {
	entity organization {
		slug    slug  required  unique  key
		plan    text
		seats   number
		renews  date
		paid    boolean
	}
	entity ticket {
		title  text  required
	}
	define service stripe "Stripe" {
		organization::subscribed
		organization::invoiced
	}
	command organization::create {
		by anyone signed in
	}
	command organization::subscribed
	command organization::invoiced
	command organization::subscribe {
		by anyone signed in
		input price_key text
		stripe checkout { price = price_key  quantity = seats  done = "/billing"  canceled = "/billing" }
	}
	command organization::billing {
		by anyone signed in
		stripe portal { back = "/organizations/{slug}/billing" }
	}
	command organization::resize {
		by anyone signed in
		input price_key text
		stripe change { price = price_key  quantity = seats }
	}
	command organization::leave {
		by anyone signed in
		stripe cancel
	}
	command organization::stop {
		by anyone signed in
		stripe cancel now
	}
	command ticket::create {
		by anyone signed in
	}
	webhook stripe /hooks/stripe as stripe {
		for organization
		on subscription {
			dispatch organization::subscribed {
				id = of  plan = price  seats = quantity  renews = period_end
			}
		}
		on invoice {
			dispatch organization::invoiced { id = of  paid = paid }
		}
	}
}
)";

    std::vector<language::file> checked(const std::string& source, language::diagnostics& out) {
        std::vector<language::file> files;
        files.push_back(language::parse("main.one", source, out));
        language::check(files, out);
        return files;
    }

    std::string wrong(const std::string& from, const std::string& to) {
        std::string source = shop;
        auto at = source.find(from);
        REQUIRE(at != std::string::npos);
        source.replace(at, from.size(), to);
        language::diagnostics out;
        checked(source, out);
        std::string said;
        for (const auto& d : out) said += d.message + "\n";
        return said;
    }

    bool has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

} // namespace

TEST_CASE("a stripe webhook is for its customer, without by, and each call says what it's given") {
    language::diagnostics out;
    auto f = language::parse("main.one", shop, out);
    REQUIRE(out.empty());
    const auto* ns = std::get_if<language::namespace_declaration>(&f.declarations.back().node);
    REQUIRE(ns != nullptr);
    const language::webhook_declaration* hook = nullptr;
    std::vector<const language::stripe_statement*> calls;
    for (const auto& d : ns->declarations) {
        if (auto* w = std::get_if<language::webhook_declaration>(&d.node)) hook = w;
        if (auto* c = std::get_if<language::command_declaration>(&d.node)) {
            for (const auto& s : c->body) {
                if (auto* st = std::get_if<language::stripe_statement>(&s.node)) calls.push_back(st);
            }
        }
    }
    REQUIRE(hook != nullptr);
    CHECK(hook->provider == "stripe");
    CHECK(hook->scope == "organization");
    CHECK(hook->repository.empty());
    CHECK(hook->as == "stripe");
    REQUIRE(calls.size() == 5);
    CHECK(calls[0]->action == "checkout");
    REQUIRE(calls[0]->values.size() == 4);
    CHECK(calls[0]->values[0].name == "price");
    CHECK(calls[3]->action == "cancel");
    CHECK(!calls[3]->now);
    CHECK(calls[4]->now);
}

TEST_CASE("an app that sells through Stripe checks") {
    language::diagnostics out;
    checked(shop, out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));
}

TEST_CASE("a stripe call is made in a command of Stripe's customer, given what it needs") {
    CHECK(has(wrong("stripe portal {", "stripe portl {"), "stripe does checkout, portal, change and cancel, not portl; did you mean portal?"));
    CHECK(has(wrong("price = price_key  quantity = seats  done", "quantity = seats  done"), "stripe checkout needs price, like price = plan"));
    CHECK(has(wrong("stripe change { price = price_key  quantity = seats }", "stripe change { price = price_key }"), "stripe change needs quantity, like quantity = seats"));
    CHECK(has(wrong("stripe portal { back = \"/organizations/{slug}/billing\" }", "stripe portal { back = \"/organizations/{slug}/billing\"  colour = \"red\" }"), "stripe portal is given back, not colour"));
    CHECK(has(wrong("/organizations/{slug}/billing", "/organizations/{slg}/billing"), "a organization has no slg for the address to name; it names its fields, like {id}"));
    CHECK(has(wrong("stripe cancel now", "stripe portal now { back = \"/billing\" }"), "only stripe cancel says now"));
    CHECK(has(wrong("command ticket::create {\n\t\tby anyone signed in\n", "command ticket::create {\n\t\tby anyone signed in\n\t\tstripe cancel\n"),
              "stripe cancel is for a organization, Stripe's customer, so it goes in a command of organization, like organization::cancel"));
    CHECK(has(wrong("webhook stripe /hooks/stripe as stripe {", "webhook github /hooks/stripe as stripe {"),
              "stripe cancel needs the app to take Stripe's webhook, which says who its customer is"));
}

TEST_CASE("a stripe webhook is for the whole app, runs as its service, and dispatches what that service runs") {
    CHECK(has(wrong("\t\tfor organization\n", "\t\tfor organization by slug\n"), "webhook stripe finds each organization itself: for organization, without by"));
    CHECK(has(wrong("\t\tfor organization\n", ""), "webhook stripe needs to know who Stripe's customer is, like for organization"));
    CHECK(has(wrong("define service stripe \"Stripe\" {", "define service stripe \"Stripe\" in organization {"),
              "webhook stripe is for the whole app, so service stripe isn't connected in each organization"));
    CHECK(has(wrong("on invoice {", "on refund {"), "stripe sends subscription and invoice, not refund"));
    CHECK(has(wrong("\t\torganization::invoiced\n\t}", "\t}"), "service stripe doesn't run organization::invoiced; add it to define service stripe"));
    CHECK(has(wrong("dispatch organization::invoiced { id = of  paid = paid }", "dispatch organization::invoiced { paid = paid }"),
              "dispatch organization::invoiced says which organization it acts on"));
}

TEST_CASE("a github webhook still says by which field it finds a project") {
    const std::string source = "import one\nentity project {\n\tslug  text  required  key\n\trepository  text  unique\n}\n"
                               "entity issue {\n\tproject  project  required  key\n\tnumber  serial  per project  key\n}\n"
                               "entity mention {\n\tissue  issue  required  key\n\turl  text  required  key\n}\n"
                               "command mention::create\n"
                               "define service github \"GitHub\" in project {\n\tmention::create\n}\n"
                               "webhook github /hooks/github as github {\n\tfor project\n}\n";
    language::diagnostics out;
    checked(source, out);
    REQUIRE(!out.empty());
    CHECK(out[0].message == "webhook github finds a project by the field naming its repository, like for project by repository");
}

TEST_CASE("Stripe's webhook and calls become the runtime's, and its route goes to the backend") {
    language::diagnostics out;
    auto files = checked(shop, out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));
    auto generated = generators::generate_api(files, root + "/examples/tasks", root + "/examples/tasks/build/api");
    REQUIRE(generated.errors.empty());
    auto go = std::find_if(generated.files.begin(), generated.files.end(), [](const auto& f) { return f.path == "billing/billing.go"; });
    REQUIRE(go != generated.files.end());
    const auto& code = go->content;
    CAPTURE(code);
    CHECK(has(code, R"(var WebhookStripe = one.Stripe("/hooks/stripe").For(one.Entity[Organization]()).As("stripe").)"));
    CHECK(has(code, "OnSubscription(func(c *one.Ctx, s one.Subscription) error {"));
    CHECK(has(code, "one.DispatchUpdate(c, s.Of, func(e *Organization) {\n\t\t\te.Plan = s.Price\n\t\t\te.Seats = float64(s.Quantity)\n\t\t\te.Renews = s.PeriodEnd\n\t\t}, nil, nil); err != nil {"));
    CHECK(has(code, "one.DispatchUpdate(c, i.Of, func(e *Organization) {\n\t\t\te.Paid = i.Paid\n\t\t}, nil, nil); err != nil {"));
    CHECK(has(code, "priceKey, _ := c.Input(\"price_key\").(string)"));
    CHECK(has(code, R"(c.StripeCheckout(o.ID, one.Price{LookupKey: priceKey}, int(o.Seats), "https://shop.example/billing", "https://shop.example/billing"))"));
    CHECK(has(code, R"(c.StripePortal(o.ID, "https://shop.example/organizations/"+url.PathEscape(o.Slug)+"/billing"))"));
    CHECK(has(code, "import (\n\t\"net/url\"\n\t\"time\"\n\n"));
    CHECK(has(code, "c.StripeChange(o.ID, one.Price{LookupKey: priceKey}, int(o.Seats))"));
    CHECK(has(code, "c.StripeCancel(o.ID, false)"));
    CHECK(has(code, "c.StripeCancel(o.ID, true)"));
    CHECK(has(code, ", ServiceStripe, WebhookStripe)"));

    auto deploy = generators::generate_infrastructure(files, root + "/examples/tasks", root + "/examples/tasks/build");
    const generators::output_file* program = nullptr;
    const generators::output_file* hosting = nullptr;
    for (const auto& f : deploy.files) {
        if (f.path == "infrastructure/main.go") program = &f;
        if (f.path == "web/firebase.json") hosting = &f;
    }
    REQUIRE(program != nullptr);
    REQUIRE(hosting != nullptr);
    CHECK(has(program->content, "\t\tStripe:   \"/hooks/stripe\",\n"));
    CHECK(has(hosting->content, R"({ "source": "/hooks/**", "run": { "serviceId": "api", "region": "us-east4" } },)"));
}
