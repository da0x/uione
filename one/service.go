// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

package one

// ServiceSpec is a system that runs commands, not a person, like GitHub telling a
// project about the commits that mention its issues: the commands it may run, made
// with Service. A webhook runs as one.
//
// A command that only services run, and that no role a project starts with allows,
// is never allowed to a person by a project's roles: not by a role a project made
// for itself, nor by one it changed to allow it, since a role's record is its
// people's to edit and a service's commands aren't theirs to take.
type ServiceSpec struct {
	name, title string
	commands    []string // as the language writes them, like mention::create
	ns          string
}

// Service declares a system that runs commands: its name, how it's shown, and the
// commands it may run, like Service("github", "GitHub", "mention::create").
func Service(name, title string, commands ...string) *ServiceSpec {
	return &ServiceSpec{name: name, title: title, commands: commands}
}

func (s *ServiceSpec) register(r *registry, ns string) {
	s.ns = ns
	r.services = append(r.services, s)
}

// onlyServices says whether a permission is one only services are given: a service
// runs its command, and no role a project starts with allows it.
func (a *App) onlyServices(p Permission) bool {
	ran := false
	for _, s := range a.reg.services {
		for _, c := range s.commands {
			ran = ran || permissionOf(s.ns, c) == p
		}
	}
	if !ran {
		return false
	}
	for _, r := range a.reg.defined {
		for _, d := range r.defaults {
			for _, c := range d.permissions {
				if permissionOf(r.ns, c) == p {
					return false
				}
			}
		}
	}
	return true
}
