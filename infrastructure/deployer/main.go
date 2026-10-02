// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

// deployer deploys a project that `one build` wrote, from its infrastructure folder.
//
//	go run github.com/da0x/uione/infrastructure/deployer [--stack production] [--yes] [--json]
//
// It shows what Pulumi would change and asks before changing anything, unless
// --yes. With --json, it prints one line of JSON per step on standard output, and
// everything else on standard error, for a program to follow.
package main

import (
	"bufio"
	"context"
	"errors"
	"flag"
	"fmt"
	"io"
	"os"
	"os/signal"
	"path/filepath"
	"strings"

	"github.com/da0x/uione/infrastructure/deploy"
)

func main() {
	// Pulumi's dependencies register flags of their own on the shared set, so the
	// deployer's are kept apart from them.
	flags := flag.NewFlagSet("deployer", flag.ExitOnError)
	stack := flags.String("stack", "production", "the Pulumi stack")
	build := flags.String("build", "..", "the folder one build wrote")
	yes := flags.Bool("yes", false, "deploy without asking")
	asJSON := flags.Bool("json", false, "print progress as JSON lines")
	flags.Parse(os.Args[1:])

	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt)
	defer stop()

	dir, err := filepath.Abs(*build)
	if err != nil {
		fail(err)
	}
	o := deploy.Options{Build: dir, Stack: *stack, Log: os.Stdout}
	if *asJSON {
		o.Progress, o.Log = os.Stdout, os.Stderr
	}
	if !*yes {
		o.Confirm = func(preview string) bool { return ask(preview, o.Log) }
	}
	needed, err := deploy.Run(ctx, o, deploy.Real())
	if errors.Is(err, deploy.ErrRefused) {
		fmt.Fprintln(o.Log, "Nothing was changed.")
		os.Exit(1)
	}
	if err != nil {
		fail(err)
	}
	if !*asJSON && needed != "" {
		fmt.Println()
		fmt.Println("Changes the domain needs at its DNS host:")
		fmt.Println(needed)
	}
}

func ask(preview string, out io.Writer) bool {
	fmt.Fprintf(out, "\n%s\nDeploy these changes? [y/N] ", strings.TrimSpace(preview))
	answer, _ := bufio.NewReader(os.Stdin).ReadString('\n')
	answer = strings.ToLower(strings.TrimSpace(answer))
	return answer == "y" || answer == "yes"
}

func fail(err error) {
	fmt.Fprintln(os.Stderr, "deploy:", err)
	os.Exit(1)
}
