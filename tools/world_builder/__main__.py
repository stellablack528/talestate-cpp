"""Run from the project root: python -m tools.world_builder --help."""
import argparse
import json
from pathlib import Path
import sys
from urllib.error import URLError

from .builder import BuilderError, compile, export, generate, merge_region, preview, read_json, validate, write_json


def main() -> int:
    parser = argparse.ArgumentParser(description="World builder: generate → edit draft → compile → validate → export")
    commands = parser.add_subparsers(dest="command", required=True)
    gen = commands.add_parser("generate", help="Generate an editable draft; --preview needs no network or key")
    gen.add_argument("--preview", action="store_true", help="Use the bundled lighthouse example; does not interpret --source")
    gen.add_argument("--source", type=Path, action="append", default=[], help="UTF-8 text/Markdown source; repeat for multiple documents")
    gen.add_argument("--out", type=Path, required=True)
    gen.add_argument("--base-url")
    gen.add_argument("--model")
    gen.add_argument("--world-id")
    gen.add_argument("--npc-naming", type=Path, help="JSON naming policy for this world")
    gen.add_argument("--naming-reference", type=Path, action="append", default=[], help="UTF-8 naming reference text; repeat for multiple documents")
    gen.add_argument("--revision", type=int, default=1)
    gen.add_argument("--timeout", type=float, default=120)
    comp = commands.add_parser("compile", help="Compile an edited draft without model access")
    comp.add_argument("draft", type=Path)
    comp.add_argument("--out", type=Path, required=True)
    val = commands.add_parser("validate", help="Validate a draft, bundle or world.json")
    val.add_argument("input", type=Path)
    exp = commands.add_parser("export", help="Export a compiled bundle as world.json + NPC seed files")
    exp.add_argument("bundle", type=Path)
    exp.add_argument("--out", type=Path, required=True)
    merge = commands.add_parser("merge-region", help="Validate and merge a regional fragment without replacing author records")
    merge.add_argument("draft", type=Path)
    merge.add_argument("fragment", type=Path)
    merge.add_argument("--region", required=True)
    merge.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.command == "generate":
            if args.preview:
                if args.source or args.npc_naming or args.naming_reference:
                    parser.error("--preview uses a fixed world and cannot take source or naming inputs")
                result = preview()
                if args.world_id:
                    result["definition"]["world_id"] = args.world_id
                result["definition"]["revision"] = args.revision
            else:
                sources = [{"id": f"source_{i + 1}", "name": p.name,
                            "text": p.read_text(encoding="utf-8-sig")} for i, p in enumerate(args.source)]
                # Keep original input before the network call, including if interrupted.
                write_json(args.out.with_suffix(".sources.json"), {"sources": sources})
                result = generate(sources, base_url=args.base_url, model=args.model,
                                  world_id=args.world_id, revision=args.revision, timeout=args.timeout,
                                  npc_naming=read_json(args.npc_naming) if args.npc_naming else None,
                                  naming_references="\n\n".join(p.read_text(encoding="utf-8-sig") for p in args.naming_reference))
            write_json(args.out, result)
            print(json.dumps({"draft": str(args.out), "generation": result["generation"]}, ensure_ascii=False))
        elif args.command == "compile":
            result = compile(read_json(args.draft))
            write_json(args.out, result)
            print(json.dumps({"bundle": str(args.out)}, ensure_ascii=False))
        elif args.command == "validate":
            value = read_json(args.input)
            if value.get("format") == "world-builder-draft/v1":
                compile(value)
                issues = []
            elif "definition" in value:
                compile({"format": "world-builder-draft/v1", **value})
                issues = []
            else:
                issues = validate(value)
            print(json.dumps({"valid": not issues, "diagnostics": issues}, ensure_ascii=False, indent=2))
            return int(bool(issues))
        elif args.command == "merge-region":
            result = merge_region(read_json(args.draft), args.region, read_json(args.fragment))
            write_json(args.out, result)
            print(json.dumps({"draft": str(args.out), "region": args.region}, ensure_ascii=False))
        else:
            print(json.dumps(export(read_json(args.bundle), args.out), ensure_ascii=False, indent=2))
    except BuilderError as exc:
        print(json.dumps({"valid": False, "diagnostics": exc.issues}, ensure_ascii=False, indent=2), file=sys.stderr)
        return 1
    except (ValueError, KeyError, TypeError, OSError, URLError) as exc:
        print(f"world-builder: {exc}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("Interrupted. Existing drafts/compiled files remain available.", file=sys.stderr)
        return 130
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
