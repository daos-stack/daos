#DATE_ONE_MONTH_AGO=$(date -d '1 month ago' +%Y-%m-%d)

DATE_ONE_MONTH_AGO=$(date -v -12m +%Y-%m-%d)
PRS=$(gh pr list --state open --limit 1000 --json number,title,updatedAt,author --search "author:ryon-jensen updated:<$DATE_ONE_MONTH_AGO"  --jq '.[] | "PR #\(.number) [\(.updatedAt)] (\(.author.login)): \(.title)"')
if [ -z "$PRS" ]; then
  echo "No stale PRs found."
#  echo "prs=No stale PRs found." >> $GITHUB_ENV
else
  echo "Found stale PRs:"
  echo "$PRS"
#  echo "prs=$PRS" >> $GITHUB_ENV
fi