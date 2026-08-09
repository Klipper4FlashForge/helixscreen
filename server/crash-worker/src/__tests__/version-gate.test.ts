// SPDX-License-Identifier: GPL-3.0-or-later
// Tests for the release-version gate that keeps crash reports from builds we
// cannot symbolicate out of the issue tracker (see issue #1240: a v0.1.4 report
// from a binary that no release ever produced).
//
// The gate has two halves:
//   - isVersionShapeValid() — cheap, local, rejects garbage before any API call
//   - isKnownRelease()      — asks GitHub whether the tag exists, and fails OPEN
//                             on any transport/permission problem

import { describe, it, expect, vi, afterEach } from "vitest";
import { isVersionShapeValid, crashIssueTitle } from "../index";
import { isKnownRelease } from "../github-app";
import type { CrashReport } from "../symbol-resolver";

afterEach(() => {
  vi.unstubAllGlobals();
});

/** Stub global fetch with a canned Response. */
function stubFetch(status: number, body: unknown) {
  vi.stubGlobal(
    "fetch",
    vi.fn(async () => new Response(JSON.stringify(body), { status }))
  );
}

describe("isVersionShapeValid", () => {
  it("accepts released version shapes", () => {
    expect(isVersionShapeValid("0.99.106")).toBe(true);
    expect(isVersionShapeValid("0.9.0")).toBe(true);
    expect(isVersionShapeValid("1.0.0-rc1")).toBe(true);
    expect(isVersionShapeValid("1.2.3+build.7")).toBe(true);
  });

  it("rejects non-semver junk", () => {
    expect(isVersionShapeValid("")).toBe(false);
    expect(isVersionShapeValid("dev")).toBe(false);
    expect(isVersionShapeValid("0.99")).toBe(false);
    expect(isVersionShapeValid("v0.99.106")).toBe(false);
  });

  it("rejects values that would inject into the issue title", () => {
    expect(isVersionShapeValid("0.1.4\n\n@prestonbrown")).toBe(false);
    expect(isVersionShapeValid("0.1.4 | evil")).toBe(false);
    expect(isVersionShapeValid("0.1.4" + "9".repeat(200))).toBe(false);
  });
});

describe("crashIssueTitle", () => {
  const BASE: CrashReport = {
    signal: 11,
    signal_name: "SIGSEGV",
    app_version: "0.99.106",
  };

  it("includes fault code and address when present", () => {
    expect(
      crashIssueTitle({
        ...BASE,
        fault_code_name: "SEGV_MAPERR",
        fault_addr: "0x78696c65682f65",
      })
    ).toBe("Crash: SIGSEGV (SEGV_MAPERR at 0x78696c65682f65) in v0.99.106");
  });

  it("falls back to the short form without fault details", () => {
    expect(crashIssueTitle(BASE)).toBe("Crash: SIGSEGV in v0.99.106");
  });

  it("strips newlines and pipes from client-supplied title fields", () => {
    const title = crashIssueTitle({
      ...BASE,
      signal_name: "SIG\nFAKE|X",
      fault_code_name: "CODE\r\nY",
      fault_addr: "0x1",
    });
    expect(title).not.toMatch(/[\r\n]/);
    expect(title).toContain("SIG FAKE\\|X");
  });

  it("caps absurdly long title fields", () => {
    const title = crashIssueTitle({ ...BASE, signal_name: "S".repeat(500) });
    expect(title.length).toBeLessThan(200);
  });
});

describe("isKnownRelease", () => {
  it("returns true when the exact tag is present", async () => {
    stubFetch(200, [{ ref: "refs/tags/v0.99.106" }]);
    await expect(isKnownRelease("t", "o", "r", "0.99.106")).resolves.toBe(true);
  });

  it("returns false when GitHub answers with an empty match list", async () => {
    stubFetch(200, []);
    await expect(isKnownRelease("t", "o", "r", "0.1.4")).resolves.toBe(false);
  });

  it("rejects a prefix match that is not the exact tag", async () => {
    // matching-refs is a prefix query: v0.1.4 also matches v0.1.40
    stubFetch(200, [{ ref: "refs/tags/v0.1.40" }]);
    await expect(isKnownRelease("t", "o", "r", "0.1.4")).resolves.toBe(false);
  });

  it("fails open when GitHub returns an error status", async () => {
    stubFetch(403, { message: "Resource not accessible by integration" });
    await expect(isKnownRelease("t", "o", "r", "0.1.4")).resolves.toBe(true);
  });

  it("fails open when the request throws", async () => {
    vi.stubGlobal(
      "fetch",
      vi.fn(async () => {
        throw new Error("network down");
      })
    );
    await expect(isKnownRelease("t", "o", "r", "0.1.4")).resolves.toBe(true);
  });

  it("fails open when the body is not an array", async () => {
    stubFetch(200, { message: "Not Found" });
    await expect(isKnownRelease("t", "o", "r", "0.1.4")).resolves.toBe(true);
  });
});
