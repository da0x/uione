# 0010: Who may do and read what

- Status: Accepted
- Date: 2026-10-02
- Deciders: Daher Alfawares

## Context

A role given everywhere suits a library's librarians. A project tracker needs roles
that hold within one project: a maintainer of one project can do nothing in
another. Some projects are public and some private, and a private project's pages
must never reach anyone outside it.

## Decision

A role is either given everywhere, in `users/{id}.role_id`, or held within an
entity:

```one
role maintainer per project from member  issue::create  issue::update
```

A member that points at a project and a person, and names the role, grants it
within that project: for the project, for what points at it, like an issue, and
for what points at that, like an issue's comment. Membership is read in the
command's own transaction. A role held within something is never given everywhere.

Each view document says who may read it, and the rules read that answer off the
document: everyone (`public`), its owner (`per user`), a permission, or, in a view
per entity, the people of its project (`readers member`), opened to everyone while
a condition holds (`public when project.visibility == public`). A view that says
who reads it decides that alone.

A view shows a person by name and picture only (`member.name`,
`member.picture`), taken from their sign-in. Nothing else about them reaches a
view.

## Consequences

A browser never reads an entity, only views, and every view decides its readers in
the backend, where the rules can't be argued with.

When people join a project or it turns private, that project's documents are
rebuilt, and only those. A list of every project has to pick the public ones
itself, or private names would leak into it.
