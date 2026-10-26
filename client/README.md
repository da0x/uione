# one::client

How the desktop and terminal apps uione writes reach their app, as
`@uione/react/firebase` does for the web. AGPL-3.0-only, as all of uione is.

- **Signing in:** through the person's browser (PKCE, the code coming back to
  127.0.0.1), with a refresh token kept from before, or with a service's key.
- **Views:** read live by name, `projects::issue_page` for `tracker-42`, through
  libember, whose header is pinned in `include/ember.hpp` (0.1.1).
- **Commands:** run by name, `projects::comment::create`, with their fields as JSON;
  a refusal says what the app said.
- **Roles:** what a person's `<member>_roles` view allows them, as the web decides it.

```sh
cmake -S client -B client/build && cmake --build client/build
client/tests/run client/build/tests   # the tasks example's backend, on the emulators
```

TLS is Botan 3, or OpenSSL with `-DONE_CLIENT_TLS=openssl`.
