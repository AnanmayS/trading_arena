import { expect, test } from '@playwright/test';

// Load, play, trade, place and cancel a resting order and a stop, reach the end screen.
// t=15 shortens the round to 15 simulated seconds.
test('a first-time visitor can play a full (short) round', async ({ page }) => {
  const errors: string[] = [];
  page.on('pageerror', (e) => errors.push(e.message));

  const loadStart = Date.now();
  await page.goto('/?seed=4242&diff=normal&t=15');
  const play = page.getByRole('button', { name: 'PLAY', exact: true });
  await expect(play).toBeEnabled();
  await play.click();

  // First visit: the tutorial walks through the ladder and asks for a buy.
  await expect(page.getByRole('dialog', { name: 'Tutorial' })).toBeVisible();
  await page.getByRole('button', { name: 'Next' }).click();
  await page.getByRole('button', { name: /BUY MKT/ }).click();
  await expect(page.getByText(/Bought 1 @/)).toBeVisible();
  expect(Date.now() - loadStart).toBeLessThan(10_000); // traded within 10 seconds of opening the link
  await page.getByRole('button', { name: 'Next' }).click();
  await page.getByRole('button', { name: 'Start trading' }).click();
  await expect(page.getByText('+1 LONG')).toBeVisible();

  // Rest a buy far below the market by clicking the bid side of the ladder's
  // bottom row, then cancel it by clicking the same order.
  const canvas = page.locator('[data-coach="ladder"] canvas');
  const box = (await canvas.boundingBox())!;
  const x = box.x + box.width * 0.08; // "my bids" column
  const y = box.y + box.height - 15; // bottom row
  await page.mouse.click(x, y);
  await expect(page.getByText('1 working')).toBeVisible();
  await page.mouse.click(x, y);
  await expect(page.getByRole('button', { name: /^CANCEL ALL/ })).toBeDisabled();

  // Shift-click the ask side of the bottom row: a protective sell stop below
  // the market. Clicking it again cancels it.
  const stopX = box.x + box.width * 0.92; // "my asks" column
  await page.keyboard.down('Shift');
  await page.mouse.click(stopX, y);
  await page.keyboard.up('Shift');
  await expect(page.getByText('· 1 stop')).toBeVisible();
  await page.mouse.click(stopX, y);
  await expect(page.getByRole('button', { name: /^CANCEL ALL/ })).toBeDisabled();

  // Hotkeys: S sells back to flat.
  await page.keyboard.press('s');
  await expect(page.getByText('FLAT', { exact: true })).toBeVisible();

  // The round ends and the summary appears.
  await expect(page.getByRole('dialog', { name: 'Round results' })).toBeVisible({ timeout: 45_000 });
  await expect(page.getByText(/You beat \d+ of \d+ bots/)).toBeVisible();
  await expect(page.getByText('Final P&L')).toBeVisible();
  expect(errors).toEqual([]);
});

test('the game works with storage disabled', async ({ page }) => {
  await page.addInitScript(() => {
    Object.defineProperty(window, 'localStorage', {
      get() {
        throw new Error('storage disabled');
      },
    });
  });
  await page.goto('/?seed=7&diff=easy&t=15');
  await page.getByRole('button', { name: 'PLAY', exact: true }).click();
  await page.getByRole('button', { name: 'Skip' }).click();
  await page.keyboard.press('b');
  await expect(page.getByText('+1 LONG')).toBeVisible();
  await expect(page.getByRole('dialog', { name: 'Round results' })).toBeVisible({ timeout: 45_000 });
});

test('watching the replay reproduces the round and its final P&L', async ({ page }) => {
  const errors: string[] = [];
  page.on('pageerror', (e) => errors.push(e.message));
  await page.goto('/?seed=99&diff=hard&t=15');
  await page.getByRole('button', { name: 'PLAY', exact: true }).click();
  await page.getByRole('button', { name: 'Skip' }).click();

  await page.keyboard.press('b');
  await expect(page.getByText('+1 LONG')).toBeVisible();
  await page.keyboard.press('3');
  await page.keyboard.press('b');
  await expect(page.getByText('+4 LONG')).toBeVisible();
  await page.waitForTimeout(1500);
  await page.keyboard.press('s');
  await expect(page.getByText('+1 LONG')).toBeVisible();

  const results = page.getByRole('dialog', { name: 'Round results' });
  await expect(results).toBeVisible({ timeout: 45_000 });
  await expect(page.getByText('Price vs. the hidden fair value')).toBeVisible();
  await expect(page.getByRole('table', { name: 'Results by bot type' }).getByText('Insider')).toBeVisible();
  const pnl = await results.getByText('Final P&L').locator('xpath=following-sibling::*[1]').textContent();
  expect(pnl).toMatch(/\$/);

  await results.getByRole('button', { name: 'Watch replay' }).click();
  await expect(results).toBeHidden();
  const bar = page.getByRole('status', { name: 'Replay' });
  await expect(bar).toBeVisible();
  await expect(page.getByRole('button', { name: /BUY MKT/ })).toBeDisabled();
  await bar.getByRole('radio', { name: '8x' }).click();
  // The replayed orders show up as fills without anyone clicking.
  await expect(page.getByText('+4 LONG')).toBeVisible({ timeout: 10_000 });

  await expect(results).toBeVisible({ timeout: 20_000 });
  await expect(page.getByTestId('replay-check')).toHaveText(/Replay reproduced your P&L exactly/);
  await expect(results.getByText('Final P&L').locator('xpath=following-sibling::*[1]')).toHaveText(pnl!);
  expect(errors).toEqual([]);
});
