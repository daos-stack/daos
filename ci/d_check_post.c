
/* Test file for d_macro checking.
 *
 * Two copies of this file exist, d_check.c and d_check_post.c,
 * the former being a file which is correct and should compile but
 * has a number of code constructs we're trying to move away from
 * and the latter being a file which has had them automatically
 * corrected.
 *
 * This test is done via the m_check_check.sh script, and the
 * corrections themselves are done in d_check_macro_calls.py
 *
 * Mention of D_FREE in comment that should be ignored.
 *
 */

main() {
	void *buff;

	/* Conditional free, should be corrected */
	D_FREE(buff);

	/* This would be removed if it was adjacent to other
	 * code which was being changed, but the use of
	 * D_FREE_PTR here should be left
	 */
	D_FREE_PTR(buff);

	/* for loop that uses both two sets of braces, but also
	 * a conditional and assigns DATA to zero afterwards
	 */
	for (i = 0; i++ ; i < 10)
		D_FREE(data);

	/* for loop where braces are needed */
	for (i = 0; i++ ; i < 10) {
		D_FREE(data[i]->buffp);
		D_FREE(data[i];
		buff = data;
	}
	/* A conditional that has two parts, one of which is
	 * the pointer that's being freed.  The D_FREE_PTR()
	 * use should be corrected here as well.
	 */
	if (buff)
		D_FREE(data);
	D_FREE(buff);

	/* two part conditional with braces */
	if (buff)
		D_FREE(data);

	/* A conditional for something else, should be ignored */
	if (my_test)
		D_FREE(buff);

	/* Assignment after free */
	D_FREE(buff);

	/* Plain old extra braces */
	if (data)
		D_FREE(buff);

	/* Conditional free, but with extra code.  Should be left */
	if (data) {
		D_FREE(data);
		buff = data;
	}
}
