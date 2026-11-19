#!/usr/bin/env python3
"""GitHub helpers that /fix-issue, /open-ms and /close-ms share.

  kind <N>                       what issue N is: a milestone's main issue, a sub-issue of one, or other
  subs <N>                       the sub-issues of main issue N: state, type, labels, what blocks them
  copy-fields <from> <to> [--status S]
                                 put issue or PR <to> in the project with the single-select fields of
                                 <from> (단계, 영역, 플랫폼, res 릴리스), Status left alone unless --status
  links <PR>                     the issues PR <PR> is linked to (its Development)

Each prints JSON. The repository is the one gh sees from the working directory.
"""

import json
import re
import subprocess
import sys

PROJECT_TITLE = "Hydroid Project"
MILESTONE_TITLE = re.compile(r"R(\d+):")


def gh(*args):
    out = subprocess.run(["gh", *args], capture_output=True, text=True, encoding="utf-8")
    if out.returncode != 0:
        sys.exit(f"gh {' '.join(args[:2])} failed:\n{out.stderr}")
    return out.stdout


def graphql(query, **variables):
    args = ["api", "graphql", "-f", f"query={query}"]
    for name, value in variables.items():
        args += ["-F" if isinstance(value, int) else "-f", f"{name}={value}"]
    data = json.loads(gh(*args))
    if data.get("errors"):
        sys.exit(json.dumps(data["errors"], ensure_ascii=False, indent=2))
    return data["data"]


def repo():
    owner, name = gh("repo", "view", "--json", "nameWithOwner", "-q", ".nameWithOwner").strip().split("/")
    return owner, name


def emit(value):
    sys.stdout.reconfigure(encoding="utf-8")
    print(json.dumps(value, ensure_ascii=False, indent=2))


def is_main(issue):
    """A milestone's main issue: no parent, a milestone "R<k>: ...", and a title that starts "R<k>:"."""
    milestone = (issue.get("milestone") or {}).get("title", "")
    m = MILESTONE_TITLE.match(milestone)
    return not issue.get("parent") and m is not None and issue["title"].startswith(f"R{m.group(1)}:")


def kind(number):
    owner, name = repo()
    issue = graphql(
        """query($o:String!,$r:String!,$n:Int!){repository(owner:$o,name:$r){issue(number:$n){
             number title state url issueType{name} milestone{number title}
             labels(first:20){nodes{name}} assignees(first:10){nodes{login}}
             parent{number title state milestone{number title} parent{number}}
             subIssues(first:1){totalCount}}}}""",
        o=owner, r=name, n=number,
    )["repository"]["issue"]
    if issue is None:
        sys.exit(f"#{number} is not an issue")
    parent = issue["parent"]
    if is_main(issue):
        what = "main"
    elif parent and not parent.get("parent") and is_main({**parent, "parent": None}):
        what = "sub"
    else:
        what = "other"
    milestone = (issue["milestone"] or (parent or {}).get("milestone") or {})
    m = MILESTONE_TITLE.match(milestone.get("title", ""))
    emit({
        "number": issue["number"],
        "kind": what,
        "title": issue["title"],
        "state": issue["state"],
        "url": issue["url"],
        "type": (issue["issueType"] or {}).get("name"),
        "labels": [l["name"] for l in issue["labels"]["nodes"]],
        "assignees": [a["login"] for a in issue["assignees"]["nodes"]],
        "milestone": issue["milestone"],
        "step": int(m.group(1)) if m else None,
        "parent": {"number": parent["number"], "title": parent["title"], "state": parent["state"]}
        if parent else None,
        "subIssues": issue["subIssues"]["totalCount"],
    })


def subs(number):
    owner, name = repo()
    issue = graphql(
        """query($o:String!,$r:String!,$n:Int!){repository(owner:$o,name:$r){issue(number:$n){
             number title subIssues(first:100){nodes{
               number title state url issueType{name} labels(first:20){nodes{name}}
               assignees(first:10){nodes{login}}
               blockedBy(first:20){nodes{number state}} blocking(first:20){nodes{number state}}}}}}}""",
        o=owner, r=name, n=number,
    )["repository"]["issue"]
    emit([
        {
            "number": s["number"],
            "state": s["state"],
            "type": (s["issueType"] or {}).get("name"),
            "labels": [l["name"] for l in s["labels"]["nodes"]],
            "assignees": [a["login"] for a in s["assignees"]["nodes"]],
            "blockedBy": [b["number"] for b in s["blockedBy"]["nodes"] if b["state"] == "OPEN"],
            "blocking": [b["number"] for b in s["blocking"]["nodes"] if b["state"] == "OPEN"],
            "title": s["title"],
        }
        for s in issue["subIssues"]["nodes"]
    ])


ITEMS = """projectItems(first:10){nodes{id project{id title}
             fieldValues(first:30){nodes{... on ProjectV2ItemFieldSingleSelectValue{
               optionId name field{... on ProjectV2SingleSelectField{id name}}}}}}}"""


def content(owner, name, number):
    node = graphql(
        """query($o:String!,$r:String!,$n:Int!){repository(owner:$o,name:$r){issueOrPullRequest(number:$n){
             __typename ... on Issue{id %s} ... on PullRequest{id %s}}}}""" % (ITEMS, ITEMS),
        o=owner, r=name, n=number,
    )["repository"]["issueOrPullRequest"]
    if node is None:
        sys.exit(f"#{number} is neither an issue nor a pull request")
    return node


def project_item(node):
    for item in node["projectItems"]["nodes"]:
        if item["project"]["title"] == PROJECT_TITLE:
            return item
    return None


def set_option(project, item, field, option):
    graphql(
        """mutation($p:ID!,$i:ID!,$f:ID!,$o:String!){updateProjectV2ItemFieldValue(input:{
             projectId:$p,itemId:$i,fieldId:$f,value:{singleSelectOptionId:$o}}){projectV2Item{id}}}""",
        p=project, i=item, f=field, o=option,
    )


def copy_fields(source, target, status=None):
    owner, name = repo()
    src = project_item(content(owner, name, source))
    if src is None:
        sys.exit(f"#{source} is not in {PROJECT_TITLE}")
    project = src["project"]["id"]
    dst_node = content(owner, name, target)
    dst = project_item(dst_node)
    if dst is None:
        added = graphql(
            """mutation($p:ID!,$c:ID!){addProjectV2ItemById(input:{projectId:$p,contentId:$c}){item{id}}}""",
            p=project, c=dst_node["id"],
        )
        dst = {"id": added["addProjectV2ItemById"]["item"]["id"]}
    copied = {}
    for value in src["fieldValues"]["nodes"]:
        field = value.get("field") or {}
        if not field or field["name"] == "Status":
            continue
        set_option(project, dst["id"], field["id"], value["optionId"])
        copied[field["name"]] = value["name"]
    if status:
        fields = graphql(
            """query($p:ID!){node(id:$p){... on ProjectV2{field(name:"Status"){
                 ... on ProjectV2SingleSelectField{id options{id name}}}}}}""",
            p=project,
        )["node"]["field"]
        option = next((o["id"] for o in fields["options"] if o["name"] == status), None)
        if option is None:
            sys.exit(f"Status has no option {status!r}: {[o['name'] for o in fields['options']]}")
        set_option(project, dst["id"], fields["id"], option)
        copied["Status"] = status
    emit({"target": target, "project": PROJECT_TITLE, "fields": copied})


def links(number):
    owner, name = repo()
    pr = graphql(
        """query($o:String!,$r:String!,$n:Int!){repository(owner:$o,name:$r){pullRequest(number:$n){
             number baseRefName headRefName closingIssuesReferences(first:20){nodes{number}}}}}""",
        o=owner, r=name, n=number,
    )["repository"]["pullRequest"]
    emit({
        "pr": pr["number"],
        "base": pr["baseRefName"],
        "head": pr["headRefName"],
        "linkedIssues": [i["number"] for i in pr["closingIssuesReferences"]["nodes"]],
    })


def main(argv):
    if len(argv) >= 2 and argv[0] in ("kind", "subs", "links"):
        {"kind": kind, "subs": subs, "links": links}[argv[0]](int(argv[1].lstrip("#")))
    elif len(argv) >= 3 and argv[0] == "copy-fields":
        status = None
        if "--status" in argv:
            status = argv[argv.index("--status") + 1]
        copy_fields(int(argv[1].lstrip("#")), int(argv[2].lstrip("#")), status)
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main(sys.argv[1:])
