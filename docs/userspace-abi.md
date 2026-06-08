# Userspace ABI

Whirliscope userspace enters the kernel through `int 0x80`.

## Register Convention

- `rax`: syscall number
- `rdi`: argument 0
- `rsi`: argument 1
- `rdx`: argument 2
- `r10`: argument 3
- `r8`: argument 4
- `r9`: argument 5
- `rax`: return value

Non-negative return values are syscall-specific. Negative return values are stable.
They also have `USER_ERR_*` constants from `src/userspace/abi.h`.

## Structs

Output structs that may grow include an `abi_version` field. Userspace should
check this field before trusting the layout.

## App Registry

The kernel currently embeds user programs as ELF images.

- `SYSCALL_APP_COUNT` returns the number of programs.
- `SYSCALL_APP_INFO` copies a `struct user_app_info` for one registry index.
- `SYSCALL_APP_RUN` runs one registry entry as a child process and returns the
  child exit status to the caller.

Secondary app launch is currently blocking: the calling process is suspended
until the child exits.