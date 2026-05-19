import Link from "next/link";
import type { ButtonHTMLAttributes, ComponentProps } from "react";

const SIZES = {
  sm: "h-8 px-4 text-[0.8125rem]",
  md: "h-10 px-6 text-sm",
  lg: "h-11 gap-2 px-8 text-[0.9375rem]",
};

const VARIANTS = {
  default: "border-border bg-bg text-fg hover:border-fg",
  primary:
    "border-accent bg-accent text-accent-fg hover:bg-transparent hover:text-accent",
};

type ButtonVariant = keyof typeof VARIANTS;
type ButtonSize = keyof typeof SIZES;

type ButtonStyleProps = {
  variant?: ButtonVariant;
  size?: ButtonSize;
  className?: string;
};

function classes({ variant = "default", size = "md", className = "" }: ButtonStyleProps): string {
  return [
    "inline-flex items-center justify-center rounded-md border font-medium no-underline transition",
    VARIANTS[variant],
    SIZES[size],
    className,
  ]
    .filter(Boolean)
    .join(" ");
}

type ButtonProps = ButtonHTMLAttributes<HTMLButtonElement> & ButtonStyleProps;

export function Button({ variant, size, className, ...rest }: ButtonProps) {
  return <button className={classes({ variant, size, className })} {...rest} />;
}

type ButtonLinkProps = ComponentProps<typeof Link> & ButtonStyleProps;

export function ButtonLink({ variant, size, className, ...rest }: ButtonLinkProps) {
  return <Link className={classes({ variant, size, className })} {...rest} />;
}
