// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

package one

import (
	"fmt"
	"reflect"
)

// A role a place is never left without, and how many it keeps: an organization
// keeps at least one owner.
type keeping struct {
	role  string
	least int
}

// Keeps says that each place keeps at least least people holding role: no command
// takes the last of them away, by removing them or giving them another role.
func (r *RolesSpec) Keeps(role string, least int) *RolesSpec {
	r.keeps = append(r.keeps, keeping{role, least})
	return r
}

// keepsEnough refuses a member removed, or moved to another role or place, when it
// would leave its place with fewer of a role it keeps than it keeps. A place going
// with it, like a project deleted with its people, keeps nothing.
func (a *App) keepsEnough(c *Ctx, s *schema, action, id string, v reflect.Value, before map[string]any) error {
	if action != "delete" && action != "update" {
		return nil
	}
	for _, r := range a.reg.defined {
		if len(r.keeps) == 0 {
			continue
		}
		f, err := a.rolesFields(r)
		if err != nil {
			return err
		}
		if s != f.member {
			continue
		}
		// What it held, and where: as stored before an update, or as it is when deleted.
		place, roleID := v.FieldByIndex(f.memberPlace.index).String(), v.FieldByIndex(f.memberRole.index).String()
		if action == "update" {
			was, _ := before[f.memberPlace.name].(string)
			wasRole, _ := before[f.memberRole.name].(string)
			if was == place && wasRole == roleID {
				continue
			}
			place, roleID = was, wasRole
		}
		if c.leaving[a.store.Collection(f.scope.collection).Doc(place).Path] {
			continue
		}
		name, err := a.roleName(c, f, roleID)
		if err != nil {
			return err
		}
		for _, k := range r.keeps {
			if k.role != name {
				continue
			}
			others, err := c.tx.Documents(a.store.Collection(f.member.collection).Where(f.memberPlace.name, "==", place).Where(f.memberRole.name, "==", roleID)).GetAll()
			if err != nil {
				return err
			}
			left := 0
			for _, d := range others {
				if d.Ref.ID != id && !c.leaving[d.Ref.Path] {
					left++
				}
			}
			if left < k.least {
				return invalid("%s %s keeps at least %s; make someone else %s %s first", article(f.scope.name), f.scope.name, howMany(k.least, k.role), article(k.role), k.role)
			}
		}
	}
	return nil
}

// howMany says a number of a role in words: one owner, 2 owners.
func howMany(n int, role string) string {
	if n == 1 {
		return "one " + role
	}
	return fmt.Sprintf("%d %ss", n, role)
}
