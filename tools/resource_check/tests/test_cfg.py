from resource_check.cfg import build_cfg
from resource_check.parser import iter_function_definitions, parse_source


def _cfg_of(source: str):
    tree = parse_source(source.encode())
    funcs = list(iter_function_definitions(tree.root_node))
    return build_cfg(funcs[0])


def _block_calling(cfg, call_text: bytes):
    return next(b for b in cfg.blocks if b.calls and b.calls[0].text == call_text)


def test_every_block_reachable_from_entry():
    cfg = _cfg_of("""
        int f(int x) {
            if (x) {
                foo();
            } else {
                bar();
            }
            baz();
            return 0;
        }
    """)
    seen, pending = set(), [cfg.entry]
    while pending:
        block = pending.pop()
        if block.id not in seen:
            seen.add(block.id)
            pending.extend(block.successors)
    assert seen == {block.id for block in cfg.blocks}


def test_forward_goto_is_resolved():
    cfg = _cfg_of("""
        int f(int x) {
            if (x) {
                goto out;
            }
            foo();
        out:
            bar();
            return 0;
        }
    """)
    bar_block = _block_calling(cfg, b"bar()")
    assert any(not b.calls and bar_block in b.successors for b in cfg.blocks)


def test_backward_goto_is_resolved():
    cfg = _cfg_of("""
        int f(int x) {
        retry:
            foo();
            if (x) {
                goto retry;
            }
            return 0;
        }
    """)
    foo_block = _block_calling(cfg, b"foo()")
    assert any(not b.calls and foo_block in b.successors for b in cfg.blocks)


def test_switch_dispatch_has_n_plus_one_successors_with_default():
    cfg = _cfg_of("""
        int f(int x) {
            switch (x) {
                case 1:
                    foo();
                    break;
                case 2:
                    bar();
                    break;
                default:
                    baz();
            }
            return 0;
        }
    """)
    assert len(cfg.entry.successors) == 3


def test_switch_dispatch_has_n_plus_one_successors_without_default():
    cfg = _cfg_of("""
        int f(int x) {
            switch (x) {
                case 1:
                    foo();
                    break;
                case 2:
                    bar();
                    break;
            }
            return 0;
        }
    """)
    assert len(cfg.entry.successors) == 3


def test_switch_fallthrough_is_sequential_edge():
    cfg = _cfg_of("""
        int f(int x) {
            switch (x) {
                case 1:
                case 2:
                    foo();
                    break;
                default:
                    bar();
            }
            return 0;
        }
    """)
    foo_block = _block_calling(cfg, b"foo()")
    assert any(not b.calls and b.successors == [foo_block] for b in cfg.blocks)


def test_break_in_switch_in_loop_targets_switch_not_loop():
    cfg = _cfg_of("""
        int f(int x) {
            while (x) {
                switch (x) {
                    case 1:
                        mark_break();
                        break;
                }
                after_switch();
            }
            return 0;
        }
    """)
    loop_cond = cfg.entry.successors[0]
    break_block = _block_calling(cfg, b"mark_break()")
    after_switch = _block_calling(cfg, b"after_switch()")

    assert break_block.successors == [after_switch]
    assert after_switch is not loop_cond


def test_continue_in_loop_targets_condition_recheck():
    cfg = _cfg_of("""
        int f(int x) {
            while (x) {
                if (x) {
                    continue;
                }
                foo();
            }
            return 0;
        }
    """)
    loop_cond = cfg.entry.successors[0]
    assert any(not b.calls and b.successors == [loop_cond] for b in cfg.blocks)


def test_loop_body_has_skip_edge_and_back_edge():
    cfg = _cfg_of("""
        int f(int x) {
            while (x) {
                foo();
            }
            return 0;
        }
    """)
    condition_block = cfg.entry.successors[0]
    body_block = _block_calling(cfg, b"foo()")
    assert body_block in condition_block.successors
    assert condition_block in body_block.successors
    after_targets = [s for s in condition_block.successors if s is not body_block]
    assert len(after_targets) == 1


def test_do_while_runs_body_before_first_condition_check():
    cfg = _cfg_of("""
        int f(int x) {
            do {
                foo();
            } while (x);
            return 0;
        }
    """)
    body_block = _block_calling(cfg, b"foo()")
    assert cfg.entry.successors == [body_block]
    condition_block = body_block.successors[0]
    assert body_block in condition_block.successors
    after_targets = [s for s in condition_block.successors if s is not body_block]
    assert len(after_targets) == 1


def test_nested_loop_break_targets_inner_loop_only():
    cfg = _cfg_of("""
        int f(int x) {
            while (x) {
                while (x) {
                    inner_break();
                    break;
                }
                after_inner();
            }
            after_outer();
            return 0;
        }
    """)
    outer_cond = cfg.entry.successors[0]
    break_block = _block_calling(cfg, b"inner_break()")
    after_inner = _block_calling(cfg, b"after_inner()")
    after_outer = _block_calling(cfg, b"after_outer()")

    assert break_block.successors == [after_inner]
    assert after_inner is not outer_cond
    assert after_outer not in break_block.successors


def test_nested_loop_continue_targets_inner_loop_only():
    cfg = _cfg_of("""
        int f(int x) {
            while (x) {
                while (x) {
                    if (x) {
                        continue;
                    }
                    inner_body();
                }
                after_inner();
            }
            return 0;
        }
    """)
    inner_body = _block_calling(cfg, b"inner_body()")
    inner_cond = inner_body.successors[0]
    assert any(not b.calls and b.successors == [inner_cond] for b in cfg.blocks)


def test_goto_can_exit_a_loop_bypassing_its_normal_edges():
    cfg = _cfg_of("""
        int f(int x) {
            while (x) {
                if (x) {
                    goto done;
                }
                foo();
            }
            bar();
        done:
            baz();
            return 0;
        }
    """)
    baz_block = _block_calling(cfg, b"baz()")
    assert any(not b.calls and b.successors == [baz_block] for b in cfg.blocks)


def test_multiple_return_points_all_reachable():
    cfg = _cfg_of("""
        int f(int x) {
            if (x == 1) {
                return 1;
            } else if (x == 2) {
                return 2;
            } else {
                return 3;
            }
        }
    """)
    return_blocks = [b for b in cfg.blocks if b.is_return]
    assert len(return_blocks) == 3
