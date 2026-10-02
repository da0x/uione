# 0009: An entity's id is made from its keys, and numbers are counted per parent

- Status: Accepted
- Date: 2026-10-02
- Deciders: Daher Alfawares

## Context

An id appears in addresses, in documents that point at it, and in the names of
views kept per entity. Made-up ids mean nothing to a reader, and trackers need
addresses people can say, like issue 12 of a project. Once data is stored under an
id it can't change without moving everything that points at it.

## Decision

Fields marked `key` make the id: their values in order, joined by dashes. A book
with shelf mark HIS-0142 is `HIS-0142`; its third loan is `HIS-0142-3`. Numbers are
written out in full. An entity with no key gets an id Firestore makes up.

`serial` is a number counted up as each entity is made; `serial per project`
counts within each project. Counters live in `serials/` and are read and written
in the same transaction as the entity, so two made at once never share a number.

Keys and serials never change once made. Making an entity whose key is taken
updates it, unless the key is also `unique`, in which case it's refused.

## Consequences

Addresses read as what they show: `/books/HIS-0142`, `/issues/uione-12`.

Each parent has its own counter, so projects never wait on each other; within one
project, issues are made one at a time.

Renaming something its key comes from means making a new one, so a key should be
something chosen once, like a short name.
