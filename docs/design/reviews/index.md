# Review records

Records of reviews that a person made of the design, one page for each.
No board has been made: a record holds one person's answers about design
files, a print, datasheets and order pages. It does not show that anything
works.

The owner goes through the
[pre-order review walkthrough](../../hardware/review/index.md) and takes a
hand-off file out of it. This command checks that file against
[its schema](../../hardware/review/handoff.schema.json) and against the board
file in the repository, and writes the record here with the decisions as
JSON beside it:

```
uv run --project tools/reviewwalk reviewwalk record <hand-off file>
```

It refuses a file that is a review of another board file than the one in
the repository, unless it is told to record it as such. It adds the record
to the list below.

## Records

<!-- zf:review-records -->

No record has been made yet when this list is empty.
